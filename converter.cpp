/**
 * Copyright (C) 2019 Dean De Leo, email: hello[at]whatsthecraic.net
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "converter.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "lib/common/permutation.hpp"
#include "lib/common/timer.hpp"
#include "abtree.hpp"
#include "graphalytics_reader.hpp"
#include "output_buffer.hpp"
#include "writer.hpp"

using namespace common;
using namespace std;

/*****************************************************************************
 *                                                                           *
 *  LOG & Debug                                                              *
 *                                                                           *
 *****************************************************************************/
extern std::mutex g_mutex_log;
#define LOG(msg) { std::scoped_lock xlock_log(g_mutex_log); std::cout << msg << std::endl; }

//#define DEBUG
#define COUT_DEBUG_FORCE(msg) LOG("[Converter::" << __FUNCTION__ << "] " << msg)
#if defined(DEBUG)
#define COUT_DEBUG(msg) COUT_DEBUG_FORCE(msg)
#else
#define COUT_DEBUG(msg)
#endif

/*****************************************************************************
 *                                                                           *
 *  Initialisation                                                           *
 *                                                                           *
 *****************************************************************************/

namespace {
// Vertex-id & frequency of each vertex
struct InitVertexRecord {
    uint32_t m_offset;
    uint32_t m_frequency;
};
}

Converter::Converter(const std::string& path_input_graph, const std::string& path_output_log, Writer& writer, uint64_t input_num_vertices_final, uint64_t input_num_edges_final) :
    m_writer(writer), m_num_operations(0){
    unordered_map<uint64_t, InitVertexRecord> map_frequencies;
    unique_ptr<WeightedEdge[]> ptr_weighted_edges;

    init_read_input_graph(&ptr_weighted_edges, &map_frequencies, path_input_graph, input_num_vertices_final, input_num_edges_final);
    
    m_num_max_edges = input_num_edges_final;
    m_num_operations = m_num_edges_total;

    unique_ptr<InitVertexRecord[]> array_frequencies { new InitVertexRecord[num_vertices()] };
    
    for (const auto& it : map_frequencies) {
        array_frequencies[it.second.m_offset] = it.second; // copy (offset, frequency)
    }
    init_edges_final_no_permute(ptr_weighted_edges);

    init_writer(path_output_log);
}

Converter::~Converter(){
    delete m_frequencies; m_frequencies = nullptr;
    delete[] m_vertices; m_vertices = nullptr;

    if(m_edges_final != nullptr){
        for(uint64_t i = 0, end = num_blocks_in_final_edges(); i < end; i++){
            free(m_edges_final[i]); m_edges_final[i] = nullptr;
        }
        free(m_edges_final);
        m_edges_final = nullptr;
    }
}

void Converter::init_read_input_graph(void* ptr_array_edges, void* ptr_frequencies, const std::string& path_input_graph, uint64_t input_num_vertices_final, uint64_t input_num_edges_final) {
    LOG("Reading the input graph from: " << path_input_graph << " ... ");
    Timer timer;
    timer.start();

    assert(ptr_array_edges != nullptr);
    auto& ptr_edges_total = *reinterpret_cast<unique_ptr<WeightedEdge[]>*>(ptr_array_edges);
    assert(ptr_frequencies != nullptr);
    auto& frequencies = *reinterpret_cast<unordered_map<uint64_t, InitVertexRecord>*>(ptr_frequencies);

    GraphalyticsReader reader{path_input_graph};
    if(reader.is_directed()) ERROR("Only undirected graphs are supported. The input graph `" << path_input_graph << "' is directed");

    string prop_num_vertices = reader.get_property("meta.vertices");
    uint64_t total_vertices_in_input = stoi(prop_num_vertices);
    m_num_vertices_final = input_num_vertices_final;
    m_num_vertices_temporary = total_vertices_in_input - input_num_vertices_final;

    string prop_num_edges = reader.get_property("meta.edges");
    m_num_edges_total = stoi(prop_num_edges);
    m_num_edges_final = input_num_edges_final;
    if(num_vertices() > std::numeric_limits<uint32_t>::max()) {
        ERROR("Too many vertices: " << num_vertices() << ", vertices in the final graph: " << num_final_vertices());
    }

    COUT_DEBUG("num vertices final graph: " << num_final_vertices() << ", num edges final graph: " << m_num_edges_final);

    m_vertices = new uint64_t[num_vertices()];
    ptr_edges_total.reset( new WeightedEdge[m_num_edges_total] );
    WeightedEdge* __restrict edges_final = ptr_edges_total.get();

    uint32_t vertex_next = 0;
    uint64_t edge_next = 0;
    uint64_t vertex, source, destination;
    double weight;

    while(reader.read_vertex(vertex)){
        m_vertices[vertex_next] = vertex;
        frequencies[vertex] = InitVertexRecord{vertex_next, 0};
        vertex_next++;
    }

    while(reader.read_edge(source, destination, weight)){
        assert(source != destination && "The edge has the same source & destination");
        assert(frequencies.count(source) > 0 && "This vertex is not present in the vertex list");
        assert(frequencies.count(destination) > 0 && "This vertex is not present in the vertex list");

        frequencies[source].m_frequency++;
        frequencies[destination].m_frequency++;

        uint32_t src_id = frequencies[source].m_offset;
        uint32_t dst_id = frequencies[destination].m_offset;
        assert(src_id != dst_id);
        if(dst_id < src_id) swap(src_id, dst_id);

        assert(edge_next < m_num_edges_final);
        edges_final[edge_next] = WeightedEdge{ src_id, dst_id, weight };
        edge_next++;
    }
    // This next line no longer applies with, because the input vertices contain both final and temp vertices
    //m_num_vertices_final = vertex_next; // actual number of vertices read in the final graph
    m_num_edges_final = edge_next; // actual number of edges read from the final graph
    cout << "The final graph will contain " << num_final_vertices() << " vertices and " << m_num_edges_final << " edges" << endl;

    timer.stop();
    LOG("Input graph parsed in " << timer);
}

void Converter::init_edges_final_no_permute(std::unique_ptr<WeightedEdge[]>& ptr_edges_total) {
    LOG("Converting the final edges into blocks ... ");
    Timer timer;
    timer.start();

    WeightedEdge* edges = ptr_edges_total.get();

    uint64_t num_blocks = num_blocks_in_final_edges();
    m_edges_final = (WeightedEdge**)calloc(num_blocks, sizeof(WeightedEdge*));
    if (m_edges_final == nullptr) throw bad_alloc();

    for (uint64_t i = 0; i < num_blocks; i++) {
        bool last_block = (i == num_blocks - 1);

        uint64_t num_edges_in_block = 
            last_block ? (m_num_edges_final - i * m_num_final_edges_per_block)
                       : m_num_final_edges_per_block;

        m_edges_final[i] = (WeightedEdge*)malloc(num_edges_in_block * sizeof(WeightedEdge));
        if (m_edges_final[i] == nullptr) throw bad_alloc();

        // Copy from the contiguous 1D array directly in order.
        for (uint64_t j = 0; j < num_edges_in_block; j++) {
            uint64_t src_idx = i * m_num_final_edges_per_block + j;
            m_edges_final[i][j] = edges[src_idx];
        }
    }

    timer.stop();
    LOG("Conversion completed in " << timer);
}

void Converter::init_writer(const string& path_output){
    LOG("Initialising the log file ....");
    Timer timer;
    timer.start();

    // We cannot guarantee to generate exactly `m_num_operations', as we could need to fill some deletions at the end
    // We will store the actual number of operations (edges) generated at the end
//    m_writer.set_property("internal.edges.cardinality", m_num_operations);
    m_writer.set_property("internal.edges.final", num_edges());
    m_writer.set_property("internal.vertices.cardinality", num_vertices());
    m_writer.set_property("internal.vertices.final.cardinality", num_final_vertices());
    m_writer.set_property("internal.vertices.temporary.cardinality", num_temporary_vertices());

    m_writer.create(path_output);
    m_writer.write_vtx_final(m_vertices, num_final_vertices());
    m_writer.write_vtx_temp(m_vertices + num_final_vertices(), num_temporary_vertices());

    timer.stop();
    LOG("Log file initialised in " << timer);
}

/*****************************************************************************
 *                                                                           *
 *  Properties                                                               *
 *                                                                           *
 *****************************************************************************/

uint64_t Converter::num_blocks_in_final_edges() const {
    return (m_num_edges_final / m_num_final_edges_per_block) + (m_num_edges_final % m_num_final_edges_per_block != 0);
}

/*****************************************************************************
 *                                                                           *
 *  Generate the operations                                                  *
 *                                                                           *
 *****************************************************************************/
uint64_t Converter::generate0() {
    cout << "Generating " << m_num_operations << " operations ..." << endl;
    Timer timer;
    timer.start();

    OutputBuffer output{m_writer}; // output buffer

    int last_progress_reported = 0;
    int64_t edges_final_block = -1, edges_final_offset = 0, edges_final_block_sz = 0, edges_final_position = 0;
    uint64_t num_ops_performed = 0;

    while (num_ops_performed < m_num_operations) {
        assert(edges_final_position <= m_num_edges_final);

        // Report progress
        if (static_cast<int>(100.0 * num_ops_performed / m_num_operations) > last_progress_reported) {
            last_progress_reported = 100.0 * num_ops_performed / m_num_operations;
            LOG("Progress: " << num_ops_performed << "/" << m_num_operations << " (" << last_progress_reported<< " %), "
                "elapsed time: " << timer
            );
        }

        //-- Directly insert all operations --//

        // retrieve the next block of final edges
        if (edges_final_offset >= edges_final_block_sz) {
            if (edges_final_block >= 0) {
                COUT_DEBUG("Deallocating a block of final edges " << edges_final_block << "/" << num_blocks_in_final_edges() << " ...");
                free(m_edges_final[edges_final_block]);
                m_edges_final[edges_final_block] = nullptr;
            }

            edges_final_block++;
            bool last_block = (edges_final_block == num_blocks_in_final_edges() - 1);
            edges_final_block_sz = (last_block ? m_num_edges_final - edges_final_block * m_num_final_edges_per_block : m_num_final_edges_per_block);
            edges_final_offset = 0;
        }

        // Get next edge operation
        WeightedEdge edge_final = m_edges_final[edges_final_block][edges_final_offset];
        edges_final_position++;
        edges_final_offset++;

        // emit operation
        output.emit(m_vertices[ edge_final.source() ], m_vertices[ edge_final.destination() ], edge_final.weight());

        num_ops_performed++;
    }

    assert(edges_final_position == m_num_edges_final && "Not all final edges have been inserted");
    assert(num_ops_performed >= m_num_operations && "Generated less operations than what requested");

    timer.stop();
    LOG("Operations generated in " << timer << ". Writing the final edges in the log file ... ");

    return num_ops_performed;
}

void Converter::generate(){
    uint64_t num_ops_performed = generate0(); // wait for the output buffer to complete...
    m_writer.write_num_edges(num_ops_performed);
}
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

#include <cassert>
#include <iostream>
#include <mutex>
#include <random>
#include <string>

#include "lib/common/error.hpp"
#include "lib/common/filesystem.hpp"
#include "lib/common/system.hpp"
#include "lib/common/timer.hpp"
#include "lib/cxxopts.hpp"

#include "converter.hpp"
#include "writer.hpp"

using namespace common;
using namespace std;

// globals
double g_aging = 10.0; // number of operations to perform, w.r.t. to the size of the loaded graph
double g_ef_edges = 1.0; // expansion factor for the edges in the graph
double g_ef_vertices = 1.2; // expansion factor for the vertices in the graph
string g_path_input; // path to the input graph, in the Graphalytics format
string g_path_output; // path where to store the log of updates

uint64_t g_seed = std::random_device{}(); // the seed to use for the random generator

uint64_t input_num_vertices_final;  // Number of final vertices in the input graph
uint64_t input_num_edges_final;     // Number of final edges in the input graph
// logging
mutex g_mutex_log;

// function prototypes
static void parse_command_line_arguments(int argc, char* argv[]);
static uint64_t num_operations(); // total number of operations to produce


int main(int argc, char* argv[]) {
    Timer timer; timer.start();

    try {
        parse_command_line_arguments(argc, argv);

        // Placeholder values for the Graphlog file
        g_aging = 1.0;
        g_ef_edges = 1.0;
        g_ef_vertices = 1.0;
        g_seed = 0;

        Writer writer;
        writer.set_property("aging_coeff", g_aging);
        writer.set_property("ef_edges", g_ef_edges);
        writer.set_property("ef_vertices", g_ef_vertices);
        writer.set_property("git_last_commit", common::git_last_commit());
        writer.set_property("hostname", common::hostname());
        writer.set_property("input_graph", g_path_input);
        writer.set_property("seed", g_seed);

        Converter converter {g_path_input, g_path_output, writer, input_num_vertices_final, input_num_edges_final};
        converter.generate();
    } catch (common::Error& e){
        cerr << e << endl;
        cerr << "Type `" << argv[0] << " --help' to check how to run the program\n";
        cerr << "Program terminated" << endl;
        return 1;
    }

    cout << "\nWhole completion time " << timer << "\n";

    return 0;
}


static void parse_command_line_arguments(int argc, char* argv[]){
    using namespace cxxopts;

    Options options(argv[0], "Graph Generator of Updates (graphlog): create a log of edge updates based on the distribution of the input graph");
    options.custom_help("[options] <input> <output>");
    options.add_options()
        ("e, edges-final", "Final number of edges in the input graph", value<uint64_t>())
        ("v, vertices-final", "Final number of vertices in the input graph", value<uint64_t>())
        ("h, help", "Show this help menu")
    ;

    auto parsed_args = options.parse(argc, argv);

    if( argc == 1 || parsed_args.count("help") > 0 ){
        cout << options.help() << endl;
        exit(EXIT_SUCCESS);
    }

    if( argc != 3 ) {
        INVALID_ARGUMENT("Invalid number of arguments: " << argc << ". Expected format: " << argv[0] << " [options] <input> <output>");
    }
    if(!common::filesystem::file_exists(argv[1])){
        INVALID_ARGUMENT("The given input graph does not exist: `" << argv[1] << "'");
    }

    g_path_input = argv[1];
    g_path_output = argv[2];

    if(parsed_args.count("vertices-final") > 0){
        input_num_vertices_final = parsed_args["vertices-final"].as<uint64_t>();
    }

    if(parsed_args.count("edges-final") > 0){
        input_num_edges_final = parsed_args["edges-final"].as<uint64_t>();
    }

    cout << "Path input graph: " << g_path_input << "\n";
    cout << "Path output log: " << g_path_output << "\n";

    cout << "Number of final vertices: " << input_num_vertices_final << "\n";
    cout << "Number of final edges: " << input_num_edges_final << "\n";

    //cout << "Aging factor: " << g_aging << "\n";
    //cout << "Expansion factor for the vertices: " << g_ef_vertices << "\n";
    //cout << "Expansion factor for the edges: " << g_ef_edges << "\n";
    //cout << "Seed for the random generator: " << g_seed << "\n";
    cout << endl;
}
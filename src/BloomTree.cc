/*
(1) bf tree (done)
(2) priority queue (done)
(3) query (done)

(4) build / load the BF (could read gzipped JF into RRR?)

SDSL RRR:

convert: reads JF BF and writes out SDSL RRR vector
BloomTree BF: only deals with SDSL vectors (possibly also gzipped)

*/

#include "BloomTree.h"
#include "util.h"
#include "BF.h"

#include <fstream>
#include <list>
#include <cassert>
#include <jellyfish/file_header.hpp>

Heap<BloomTree> BloomTree::bf_cache;

// construct a bloom filter with the given filter backing.
BloomTree::BloomTree(
    const std::string & f, 
    HashPair hp,
    int nh
) :
    filename(f),
    hashes(hp),
    num_hash(nh),
    bloom_filter(0),
    parent(0),
    usage_count(0)
{
    children[0] = nullptr;
    children[1] = nullptr;
}

// free the memory for this node.
BloomTree::~BloomTree() {
    unload();
}

std::string BloomTree::name() {
    return filename;
}

// Return the node for the given child
BloomTree* BloomTree::child(int which) { 
    assert(which >= 0 || which < 2);
    return children[which]; 
}

// Set the given child
void BloomTree::set_child(int which, BloomTree* c) {
    assert(which >= 0 || which < 2);
    c->parent = this;
    children[which] = c;
}

// return the bloom filter, loading first if necessary
BF* BloomTree::bf() {
    load();
    return bloom_filter;
}

// return the number of times this bloom filter has been used.
int BloomTree::usage() {
    return usage_count;
}

// increment the usage counter, and update the filter's location in
// the heap if needed.
void BloomTree::increment_usage() {
    usage_count++;
    // if we're in the cache, let the cache know we've been used.
    if (heap_ref.is_valid()) {
        bf_cache.increase_key(heap_ref, usage_count);
    }
}

// Frees the memory associated with the bloom filter
void BloomTree::unload() { 
    // free the memory
    delete bloom_filter; 
    bloom_filter = nullptr; 
}

// Loads the bloom filtering into memory
bool BloomTree::load() {
    if (bloom_filter == nullptr) {
        std::cerr << "Loading BF: " << filename << std::endl;
        if (bf_cache.size() > BF_INMEM_LIMIT) {
            // toss the bloom filter with the lowest usage
            BloomTree* loser = bf_cache.pop();
            loser->heap_ref.invalidate();

            std::cerr << "Unloading BF: " << loser->filename << std::endl;
            loser->unload();
        }
            
        // read the BF file and set bloom_filter
        bloom_filter = new BF(filename, hashes, num_hash);
        heap_ref = bf_cache.insert(this, usage());
    }
    return true;
}


HashPair* get_hash_function(const string & matrix_file, int & nh) {
    std::cerr << "Loading hashes from " << matrix_file << std::endl; 
    std::ifstream in(matrix_file.c_str(), std::ios::in | std::ios::binary);
    jellyfish::file_header header(in);
    DIE_IF(!in.good(), "Couldn't parse bloom filter header!");
    HashPair * hp = new HashPair(header.matrix(1), header.matrix(2));
    in.close();

    nh = header.nb_hashes();
    std::cerr << "# Hash applications=" << nh << std::endl;

    jellyfish::mer_dna::k(header.key_len() / 2);
    std::cerr << "Read hashes for k=" << jellyfish::mer_dna::k() << std::endl;
    return hp;
}

/* read a file that defines the bloom tree structure. The
   file has lines of the form:
    Root
    *Child1
    ***Child3
    ****Child4
    *Child2
   where the '*' indicates the level and where "Child1" etc
   are comma-separated lists of attributes, the first one 
   of which must be the BF filename.

   This function will return a pointer to the root of the
   constructed bloom tree.
*/
BloomTree* read_bloom_tree(const std::string & filename) {
    std::ifstream in(filename.c_str());

    std::list<BloomTree*> path;
    BloomTree* tree_root = 0;
    int n = 0;
    HashPair *hashes;
    int num_hashes = 0;

    std::string node_info;
    while (getline(in, node_info)) {
        node_info = Trim(node_info);
        if (node_info.size() == 0) continue;
        size_t level = node_info.find_first_not_of("*");
        node_info.erase(0, level);

        // each node info is a comma separated list
        std::vector<std::string> fields;
        SplitString(node_info, ',', fields);
        std::string bf_filename = fields[0];
        std::cerr << "Reading BN info: " << bf_filename << std::endl;

        n++;

        BloomTree* bn = nullptr;

        // if we're at the root
        if (path.size() == 0) {
            DIE_IF(level != 0, "Root must start in column 0");
            DIE_IF(tree_root != 0, "Can't set root twice!");

            // set the hash function up
            DIE_IF(fields.size() < 2, "Must specify hash file for root.");
            hashes = get_hash_function(fields[1], num_hashes);

            // create the root node
            bn = new BloomTree(bf_filename, *hashes, num_hashes); 
            tree_root = bn;
            
        // if we're adding a child
        } else {
            bn = new BloomTree(bf_filename, *hashes, num_hashes); 

            while (path.size() >= level) {
                path.pop_back();
            }
            DIE_IF(level != path.size()+1, 
                "Must increase level by <= 1");

            if (path.back()->child(0) == nullptr) {
                path.back()->set_child(0, bn);
            } else if (path.back()->child(1) == nullptr) {
                path.back()->set_child(1, bn);
            } else {
                DIE("Tried to add >= 2 children to a node.");
            }
        }
        path.push_back(bn);
    }
    delete hashes;

    std::cerr << "Read " << n << " nodes in Bloom Tree" << std::endl;
    
    return tree_root;
}


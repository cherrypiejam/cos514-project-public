#include <vector>
#include <iterator>
#include <fstream>
#include <iostream>
#include "rpc/client.h"

std::vector<uint8_t> read_file(const char* filename) {
    std::ifstream file(filename, std::ios::in | std::ios::binary);

    // Don't eat newlines:
    file.unsetf(std::ios::skipws);

    // Get filesize:
    std::streampos fileSize;
    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Reserve vector capacity:
    std::vector<uint8_t> vec;
    vec.reserve(fileSize);

    // Read data:
    vec.insert(vec.begin(), std::istream_iterator<uint8_t>(file), std::istream_iterator<uint8_t>());

    std::cout << "Read " << vec.size() << " bytes!" << std::endl;

    return vec;
}

int main() {
    std::cout << "Hello from the RPC client!" << std::endl;

    // Creating a client that connects to the localhost on port 8080
    rpc::client client("127.0.0.1", 5826);

    std::vector<uint8_t> eapp_bytes = read_file("./extracted/gpu-worker-eapp");
    std::vector<uint8_t> runtime_bytes = read_file("./extracted/eyrie-rt");
    std::vector<uint8_t> loader_bytes = read_file("./extracted/loader.bin");

    // std::vector<uint8_t> vec = {42, 0x41, 13, 37, 7, 7, 7, 7, 7};
    client.call("eapp", eapp_bytes, runtime_bytes, loader_bytes);

    //client.call("helloworld");
    client.call("matmul");
    return 0;
}

#include <iostream>
#include <queue>
#include <string>
#include <ac-common/file.hpp>
#include <openssl/sha.h>
#include <sys/types.h>
#include <utility>
#include <unordered_map>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef RELEASE_FILESYSTEM
#include <filesystem>
#else
#include <experimental/filesystem>

namespace std {
    namespace filesystem = std::experimental::filesystem;
}
#endif

struct TItem {
    struct TFile {
        std::string Path;
        ino_t INodeNum;
    };

    std::vector<TFile> Files;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " /path/to/dir [/path/to/dir2 ...]" << std::endl;
        return 1;
    }

    std::deque<std::string> pathes;

    for (size_t i = 1; i < argc; ++i) {
        pathes.push_back(argv[i]);
    }

    unsigned char hashed[SHA256_DIGEST_LENGTH];

    std::string hashedStr;
    hashedStr.reserve(SHA256_DIGEST_LENGTH * 2);

    std::unordered_map<std::string, TItem> items;
    std::unordered_map<ino_t, bool> inodes;

    while (!pathes.empty()) {
        for (const auto& node : std::filesystem::directory_iterator(pathes.front())) {
            const auto& nodeStr = node.path().string();

            if ((nodeStr == std::string(".")) || (nodeStr == std::string(".."))) {
                continue;
            }

            if (std::filesystem::is_directory(node)) {
                pathes.push_back(nodeStr);
                continue;
            }

            NAC::TFile file(nodeStr);

            if (!file) {
                continue;
            }

            if (inodes.count(file.INodeNum()) > 0) {
                inodes[file.INodeNum()] = true;
                continue;
            }

            inodes[file.INodeNum()] = false;

            SHA256((const unsigned char*)file.Data(), file.Size(), hashed);

            static const char charTable[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

            hashedStr.resize(0);

            for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                hashedStr += charTable[hashed[i] >> 4];
                hashedStr += charTable[hashed[i] & 0xf];
            }

            auto&& it = items.find(hashedStr);

            if (it == items.end()) {
                TItem item;
                item.Files.push_back(TItem::TFile {
                    nodeStr,
                    file.INodeNum()
                });

                items[hashedStr] = std::move(item);

            } else {
                it->second.Files.push_back(TItem::TFile {
                    nodeStr,
                    file.INodeNum()
                });
            }
        }

        pathes.pop_front();
    }

    for (const auto& it : items) {
        if (it.second.Files.size() < 2) {
            continue;
        }

        std::string master;

        for (const auto& spec : it.second.Files) {
            if (inodes.at(spec.INodeNum)) {
                master = spec.Path;
                break;
            }
        }

        if (master.empty()) {
            master = it.second.Files[0].Path;
        }

        for (const auto& spec : it.second.Files) {
            if (spec.Path == master) {
                continue;
            }

            std::cerr << master << " -> " << spec.Path << std::endl;

            std::string tmpPath(spec.Path + ".XXXXXXXXXX");
            int fh = mkstemp(tmpPath.data());

            if (fh == -1) {
                perror("mkstemp");
                continue;
            }

            if (rename(spec.Path.c_str(), tmpPath.c_str()) == -1) {
                perror("rename");
                continue;
            }

            if (link(master.c_str(), spec.Path.c_str()) == -1) {
                perror("link");

                if (rename(tmpPath.c_str(), spec.Path.c_str()) == -1) {
                    perror("rename");
                    return 1;

                } else {
                    continue;
                }
            }

            if (unlink(tmpPath.c_str()) == -1) {
                perror("unlink");
            }

            if (close(fh) == -1) {
                perror("close");
            }
        }
    }

    return 0;
}

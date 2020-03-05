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
#include <ac-library/containers/rbtree/persistent.hpp>
#include <ac-common/str.hpp>
#include <string.h>
#include <ac-common/utils/string.hpp>

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
        std::cerr << "Usage: " << argv[0] << " [-d /path/to/hashdb] [-n] [-c] [-s 4096] /path/to/dir [/path/to/dir2 ...]" << std::endl;
        return 1;
    }

    std::deque<std::string> pathes;
    std::string dbPath;
    bool dryRun(false);
    bool compareContent(false);
    size_t chunkSize(4096);

    for (size_t i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0) {
            ++i;
            dbPath = argv[i];

        } else if (strcmp(argv[i], "-n") == 0) {
            dryRun = true;

        } else if (strcmp(argv[i], "-c") == 0) {
            compareContent = true;

        } else if (strcmp(argv[i], "-s") == 0) {
            ++i;
            NAC::NStringUtils::FromString(strlen(argv[i]), argv[i], chunkSize);

        } else {
            pathes.push_back(argv[i]);
        }
    }

    NAC::TPersistentRBTree* db(nullptr);

    if (!dbPath.empty() && std::filesystem::exists(dbPath) && !std::filesystem::is_empty(dbPath)) {
        db = new NAC::TPersistentRBTree(dbPath, NAC::TFile::ACCESS_RDWR);
        db->FindRoot();
    }

    unsigned char hashed[SHA256_DIGEST_LENGTH];

    std::string hashedStr;
    hashedStr.reserve(SHA256_DIGEST_LENGTH * 2);

    std::unordered_map<std::string, std::unordered_map<size_t, TItem>> items;
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

            NAC::TFile file(nodeStr, NAC::TFile::ACCESS_RDONLY_DIRECT);

            if (!file) {
                continue;
            }

            if (inodes.count(file.INodeNum()) > 0) {
                inodes[file.INodeNum()] = true;
                continue;
            }

            inodes[file.INodeNum()] = false;

            hashedStr.resize(0);

            if (db) {
                auto value = db->Get(nodeStr);

                if (value) {
                    hashedStr += (std::string)value;
                }
            }

            if (hashedStr.empty()) {
                SHA256_CTX ctx;

                if (SHA256_Init(&ctx) == 0) {
                    continue;
                }

                auto chunks = file.Chunks(chunkSize);
                bool ok(true);

                while (auto chunk = chunks.Next()) {
                    if (SHA256_Update(&ctx, (const unsigned char*)chunk.Data(), chunk.Size()) == 0) {
                        ok = false;
                        break;
                    }
                }

                if (!ok) {
                    continue;
                }

                if (SHA256_Final(hashed, &ctx) == 0) {
                    continue;
                }

                static const char charTable[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

                for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                    hashedStr += charTable[hashed[i] >> 4];
                    hashedStr += charTable[hashed[i] & 0xf];
                }

                if (db) {
                    db->Insert(nodeStr, NAC::TBlob() << hashedStr);
                }
            }

            auto&& it = items.find(hashedStr);
            TItem::TFile rec {
                nodeStr,
                file.INodeNum()
            };

            if (it == items.end()) {
                TItem item;
                item.Files.push_back(std::move(rec));

                items[hashedStr][file.Size()] = std::move(item);

            } else {
                auto&& inner_it = it->second.find(file.Size());

                if (inner_it == it->second.end()) {
                    TItem item;
                    item.Files.push_back(std::move(rec));

                    it->second[file.Size()] = std::move(item);

                } else {
                    inner_it->second.Files.push_back(std::move(rec));
                }
            }
        }

        pathes.pop_front();
    }

    if (db) {
        delete db;
        db = nullptr;

    } else if (!dbPath.empty()) {
        db = new NAC::TPersistentRBTree(dbPath, NAC::TFile::ACCESS_CREATE);
    }

    for (const auto& outer_it : items) {
        for (const auto& it : outer_it.second) {
            if (db) {
                for (const auto& spec : it.second.Files) {
                    db->Insert(spec.Path, NAC::TBlob() << outer_it.first);
                }
            }

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

                if (compareContent) {
                    NAC::TFile masterFile(master, NAC::TFile::ACCESS_RDONLY_DIRECT);
                    NAC::TFile currentFile(spec.Path, NAC::TFile::ACCESS_RDONLY_DIRECT);

                    if (!masterFile || !currentFile) {
                        continue;
                    }

                    if (masterFile.Size() != currentFile.Size()) {
                        continue;
                    }

                    auto masterChunks = masterFile.Chunks(chunkSize);
                    auto currentChunks = currentFile.Chunks(chunkSize);
                    bool ok(true);

                    while (ok) {
                        auto masterChunk = masterChunks.Next();
                        auto currentChunk = currentChunks.Next();

                        if (!masterChunk && !currentChunk) {
                            break;

                        } else if (masterChunk.Size() != currentChunk.Size()) {
                            ok = false;

                        } else {
                            ok = memcmp(masterChunk.Data(), currentChunk.Data(), masterChunk.Size()) == 0;
                        }
                    }

                    if (!ok) {
                        continue;
                    }
                }

                std::cerr << master << " -> " << spec.Path << std::endl;

                if (dryRun) {
                    continue;
                }

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
    }

    if (db) {
        delete db;
    }

    return 0;
}

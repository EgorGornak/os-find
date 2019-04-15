#include <iostream>
#include <vector>
#include <iterator>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <cstring>
#include <dirent.h>

using namespace std;

void invoke(vector<string> args) {
    vector<char *> c_args(args.size() + 1);
    for (int i = 0; i < args.size(); i++) {
        c_args[i] = const_cast<char *>(args[i].c_str());
    }
    c_args[args.size()] = nullptr;

    pid_t pid = fork();
    if (pid == -1) {
        cerr << "Can't create fork" << endl;
        return;
    } else if (pid == 0) {
        if (execve(c_args[0], c_args.data(), nullptr) == -1) {
            cerr << "failed :(" << endl;
            exit(EXIT_FAILURE);
        }
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            cerr << "Can't execute" << endl;
        }
    }
}

template<typename T>
struct my_optional {
    T value;
    bool used = false;

    void set(T x) {
        value = x;
        used = true;
    }
};

class file_filter {
    my_optional<ino_t> inode_number;
    my_optional<char *> file_name;
    my_optional<off_t> size_filter;
    my_optional<nlink_t> hardlinks_number;

    enum compare {
        LESS, EQUAL, GREATER
    };
    compare size_compare;

public:
    file_filter(int size, char *args[]) {
        for (int i = 2; i < size; i++) {
            if (strcmp("-inum", args[i]) == 0) {
                try {
                    inode_number.set(stoull(args[++i]));
                } catch (...) {
                    std::cerr << "incorrect inode_number" << endl;
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp("-name", args[i]) == 0) {
                file_name.set(args[++i]);
            } else if (strcmp("-size", args[i]) == 0) {


                bool withot_filter = false;
                if (args[i + 1][0] == '-') {
                    size_compare = LESS;
                } else if (args[i + 1][0] == '+') {
                    size_compare = GREATER;
                } else if (args[i + 1][0] == '=') {
                    size_compare = EQUAL;
                } else {
                    size_compare = EQUAL;
                    withot_filter = true;
                }

                try {
                    if (withot_filter) {
                        size_filter.set(stoull(args[++i]));
                    } else {
                        size_filter.set(stoull(args[++i] + 1));
                    }
                } catch (...) {
                    std::cerr << "incorrect size " << endl;
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp("-nlinks", args[i]) == 0) {
                try {
                    hardlinks_number.set(stoull(args[++i]));
                } catch (...) {
                    std::cerr << "incorrect hard link number" << endl;
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp("-exec", args[i]) == 0) {
                i++;
            } else {
                std::cerr << "unknown flag";
                exit(EXIT_FAILURE);
            }
        }

    }

    bool filter(struct dirent *file, char *path, const char *destination) {
        struct stat sb;
        stat(path, &sb);

        if (inode_number.used && inode_number.value != file->d_ino) {
            return false;
        }

        if (file_name.used && strcmp(file_name.value, destination) != 0) {
            return false;
        }

        if (size_filter.used) {
            if (size_compare == LESS && sb.st_size >= size_filter.value) {
                return false;
            }
            if (size_compare == EQUAL && sb.st_size != size_filter.value) {
                return false;
            }
            if (size_compare == GREATER && sb.st_size <= size_filter.value) {
                return false;
            }
        }

        if (hardlinks_number.used && sb.st_nlink != hardlinks_number.value) {
            return false;
        }

        return true;
    }

};

char *concat(char *s1, char *s2) {
    char *result = new char[strlen(s1) + strlen(s2) + 2];
    strcpy(result, s1);
    strcat(result, "/");
    strcat(result, s2);
    return result;
}

class walker {
public:
    walker(int size, char *args[], char *envp_[]) : filter(size, args) {
        for (int i = 2; i < size; i++) {
            if (strcmp("-exec", args[i]) == 0) {
                exec_path = args[i + 1];
                break;
            }
        }
    }

    void walk(char *path) {
        DIR *directory = opendir(path);
        if (directory == nullptr) {
            cerr << strerror(errno);
            return;
        }

        dirent *curr;
        while ((curr = readdir(directory)) != nullptr) {
            char *destination = curr->d_name;

            if (strcmp("..", destination) == 0 || strcmp(".", destination) == 0) {
                continue;
            }

            char *full_path = concat(path, destination);
            switch (curr->d_type) {
                case DT_DIR:
                    walk(full_path);
                    break;
                case DT_REG:
                    if (filter.filter(curr, full_path, destination)) {
                        cout << full_path << endl;
                        if (exec_path != nullptr) {
                            vector<string> args;
                            args.emplace_back(exec_path);
                            args.emplace_back(full_path);
                            invoke(args);
                        }
                    }
                    break;
            }
            delete[] full_path;
        }
        closedir(directory);
    }

private:
    file_filter filter;
    char *exec_path = nullptr;
};

int main(int argc, char *argv[], char *envp[]) {
    walker finder(argc, argv, envp);
    if (argc == 1) {
        cout << "Usage <path> <-inum num> <-name name> <-size [-=+]size> <-nlinks num> <-exec path>" << endl;
        return 0;
    }
    finder.walk(argv[1]);
    return 0;
}
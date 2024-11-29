#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#define USER_MAX_INPUT 1024
#define MAX_ARGS 64

static const char *task_file_path = "/tasks";
static char task_content[1024] = "Initial Task List\n";

void handle_sighup(int sig) {
    printf("Configuration reloaded\n");
}

void dump_memory_segment(const char *src_path, const char *dest_path) {
    int mem_fd = open(src_path, O_RDONLY);
    if (mem_fd == -1) {
        perror("Ошибка открытия файла сегмента памяти");
        return;
    }

    int dump_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dump_fd == -1) {
        perror("Ошибка создания дамп-файла для сегмента");
        close(mem_fd);
        return;
    }

    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(mem_fd, buffer, sizeof(buffer))) > 0) {
        if (write(dump_fd, buffer, bytes_read) != bytes_read) {
            perror("Ошибка записи в дамп-файл сегмента");
            break;
        }
    }

    if (bytes_read == -1) {
        perror("Ошибка чтения памяти сегмента");
    }

    close(mem_fd);
    close(dump_fd);
}

void dump_memory(int pid) {
    if (geteuid() != 0) {
        printf("Ошибка: для дампа памяти процесса нужны права root.\n");
        return;
    }

    char dir_path[64];
    snprintf(dir_path, sizeof(dir_path), "/proc/%d/map_files", pid);

    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("Ошибка открытия директории map_files");
        return;
    }

    char dump_dir[128];
    snprintf(dump_dir, sizeof(dump_dir), "/tmp/memory_dumps_%d", pid);
    mkdir(dump_dir, 0755);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_LNK) {
            char file_path[512], real_path[512], segment_dump[512];
            snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);

            ssize_t path_len = readlink(file_path, real_path, sizeof(real_path) - 1);
            if (path_len == -1) {
                perror("Ошибка чтения ссылки на сегмент памяти");
                continue;
            }
            real_path[path_len] = '\0';

            snprintf(segment_dump, sizeof(segment_dump), "%s/%s.bin", dump_dir, entry->d_name);
            dump_memory_segment(real_path, segment_dump);
            printf("Сегмент памяти %s дампирован.\n", entry->d_name);
        }
    }
    closedir(dir);
}

void print_partition_info() {
    FILE *file = fopen("/proc/partitions", "r");
    if (!file) {
        perror("Ошибка открытия /proc/partitions");
        return;
    }

    char line[256];
    printf("Информация о разделах:\n");
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "sda") || strstr(line, "sdb")) {
            printf("%s", line);
        }
    }
    fclose(file);
}

void execute_binary(const char *path, char *input) {
    char *args[MAX_ARGS];
    int i = 0;
    char *token = strtok(input, " ");
    while (token && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;

    if (access(path, X_OK) == -1) {
        perror("Ошибка: недостаточно прав для выполнения файла или файл не найден");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(path, args);
        perror("Ошибка выполнения команды");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Программа завершена с кодом %d\n", WEXITSTATUS(status));
        } else {
            printf("Программа завершилась ненормально\n");
        }
    } else {
        perror("Ошибка создания процесса");
    }
}

void run_mount_script() {
    const char *script_path = "/home/rodion/Linux_homework-main/Bogdanov_Rodion_24/mount_vfs.sh";

    if (access(script_path, X_OK) == -1) {
        perror("Ошибка: скрипт недоступен для выполнения");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl(script_path, script_path, NULL);
        perror("Ошибка выполнения скрипта");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Скрипт завершён с кодом %d\n", WEXITSTATUS(status));
        } else {
            printf("Скрипт завершился ненормально\n");
        }
    } else {
        perror("Ошибка создания процесса для скрипта");
    }
}



int main(int argc, char *argv[]) {
    char input[USER_MAX_INPUT];
    FILE *history = fopen("history.txt", "a");

    signal(SIGHUP, handle_sighup);

    while (1) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) break;

        input[strcspn(input, "\n")] = '\0';
        fprintf(history, "%s\n", input);
        fflush(history);

        if (strcmp(input, "exit") == 0 || strcmp(input, "\\q") == 0) {
            break;
        } else if (strncmp(input, "echo ", 5) == 0) {
            printf("%s\n", input + 5);
        } else if (strncmp(input, "./", 2) == 0) {
            execute_binary(input, input);
        } else if (strncmp(input, "\\bin ", 5) == 0) {
            char bin_path[USER_MAX_INPUT];
            snprintf(bin_path, sizeof(bin_path), "/bin/%s", input + 5);
            execute_binary(bin_path, input + 5);
        } else if (strcmp(input, "\\l /dev/sda") == 0) {
            print_partition_info();
        } else if (strncmp(input, "\\mem ", 5) == 0) {
            int pid = atoi(input + 5);
            if (pid > 0) dump_memory(pid);
            else printf("Некорректный PID.\n");
        } else if (strcmp(input, "\\dfh") == 0) {
            execute_binary("df", "-h");
        } else if (strcmp(input, "\\cron") == 0) {
    run_mount_script();
        } else {
            printf("Неизвестная команда: %s\n", input);
        }
    }
    fclose(history);
    return 0;
}

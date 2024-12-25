#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

#define HISTORY_SIZE 10
#define HISTORY_FILE "history.txt"

void sighup_handler(int sig) {
    printf("Configuration reloaded\n");
}

void handle_echo(char *input) {
    input[strcspn(input, "\n")] = 0;

    if (strcmp(input, "echo") == 0) {
        return;
    } 
    else if (strcmp(input, "echo ") == 0) {
        printf("\n");
    } 
    else if (strncmp(input, "echo ", 5) == 0) {
        char *arg = input + 5;
        if ((arg[0] == '"' && arg[strlen(arg) - 1] == '"') ||
            (arg[0] == '\'' && arg[strlen(arg) - 1] == '\'')) {
            arg[strlen(arg) - 1] = '\0';
            arg++;
        }
        printf("%s\n", arg);
    } else {
        printf("Incorrect using echo command!\n");
    }
}

void execute_command(char *command) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        char *argv[64];  // Предполагаем, что не будет больше 64 аргументов 
        char *token = strtok(command, " ");
        int i = 0;

        // Заполняем массив argv 
        while (token != NULL) {
            argv[i++] = token;
            token = strtok(NULL, " ");
        }
        argv[i] = NULL; // Завершаем массив NULL

        // Формируем полный путь к исполняемому файлу 
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "/bin/%s", argv[0]);

        execv(full_path, argv); // Выполняем бинарный файл 
        perror("execv failed");
        exit(EXIT_FAILURE);
    } 
    
    else if (pid > 0) {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    } 
    
    else {
        perror("fork failed");
    }
}

void is_bootable_device(char* device_name) {
    while(*device_name == ' ') device_name++;

    const char* root = "/dev/";
    char full_path[128];
    sprintf(full_path, "%s%s", root, device_name);

    FILE* device_file = fopen(full_path, "rb");

    if(device_file == NULL) {
        printf("There is no such disk!\n");
        return;
    }

    int position = 510;
    if(fseek(device_file, position, SEEK_SET) != 0) {
        printf("Error while SEEK operation!\n");
        fclose(device_file);
        return;
    }

    uint8_t data[2];
    if(fread(data, 1, 2, device_file) != 2) {
        printf("Error while fread operation\n");
        fclose(device_file);
        return;
    }
    fclose(device_file);

    if(data[1] == 0xaa && data[0] == 0x55) {
        printf("Disk %s is bootable\n", device_name);
    } else {
        printf("Disk %s isn't bootable\n", device_name);
    }
}

void dump_memory(char *proc_id) {
    char map_files_path[256];
    snprintf(map_files_path, sizeof(map_files_path), "/proc/%s/map_files", proc_id);

    // Проверим, существует ли директория
    struct stat st;
    if (stat(map_files_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("Process %s does not exist or has no memory maps.\n", proc_id);
        return;
    }
    
    char output_file[256];
    snprintf(output_file, sizeof(output_file), "memory_dump_%s.txt", proc_id);
    FILE *output = fopen(output_file, "w");
    if (!output) {
        perror("Failed to open output file");
        return;
    }

    DIR *dir = opendir(map_files_path);
    if (!dir) {
        perror("Failed to open map_files directory");
        fclose(output);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Игнорируем . и ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char file_path[512]; // Увеличиваем размер буфера
        if (snprintf(file_path, sizeof(file_path), "%s/%s", map_files_path, entry->d_name) >= sizeof(file_path)) {
            fprintf(stderr, "Warning: file path truncated: %s/%s\n", map_files_path, entry->d_name);
            continue; // Пропускаем этот файл, если путь слишком длинный
        }

        FILE *memory_file = fopen(file_path, "r");
        if (memory_file) {
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), memory_file) != NULL) {
                fputs(buffer, output);
            }
            fclose(memory_file);
        } else {
            perror("Failed to open memory file");
        }
    }

    closedir(dir);
    fclose(output);
    printf("Memory dump for process %s saved to %s\n", proc_id, output_file);
}

void create_vfs() {
    // Создаем директорию для VFS
    const char *vfs_path = "/tmp/vfs";
    struct stat st = {0};

    if (stat(vfs_path, &st) == -1) {
        mkdir(vfs_path, 0700);
    }

    // Получаем список задач из crontab
    FILE *fp = popen("crontab -l", "r");
    if (fp == NULL) {
        perror("Failed to run crontab command");
        return;
    }

    // Создаем файл для вывода задач
    char output_file[256];
    snprintf(output_file, sizeof(output_file), "%s/tasks.txt", vfs_path);
    FILE *output = fopen(output_file, "w");
    if (output == NULL) {
        perror("Failed to open output file for tasks");
        pclose(fp);
        return;
    }

    // Читаем вывод crontab и записываем в файл, игнорируя комментарии
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Игнорируем строки, начинающиеся с #
        if (buffer[0] == '#') {
            continue;
        }
        fputs(buffer, output);
    }

    fclose(output);
    pclose(fp);
    printf("Virtual file system created at %s with scheduled tasks.\n", vfs_path);
}


int main() {
    signal(SIGHUP, sighup_handler);

    char input[1024];

    char* history[HISTORY_SIZE];
    for (int i = 0; i < HISTORY_SIZE; i++) {
        history[i] = malloc(1024);
    }
    int history_index = 0;
    FILE* history_file = fopen(HISTORY_FILE, "a+");

    char line[1024];
    while (fgets(line, 1024, history_file) != NULL) {
        strcpy(history[history_index], line);
        history_index = (history_index + 1) % HISTORY_SIZE;
    }

    while (fgets(input, 1024, stdin) != NULL) {
        strcpy(history[history_index], input);
        history_index = (history_index + 1) % HISTORY_SIZE;
        fprintf(history_file, "%s", input);

        if (strcmp(input, "exit\n") == 0 || strcmp(input, "\\q\n") == 0) {
            break;
        } 
        
        else if (strncmp(input, "echo", 4) == 0) {
            handle_echo(input);
        } 
        
        else if (strncmp(input, "\\e ", 3) == 0) {
            char* var_name = input + 4;
            var_name[strcspn(var_name, "\n")] = 0;
            char* var_value = getenv(var_name);
            if (var_value != NULL) 
                printf("%s\n", var_value);
            else
                printf("Variable not found: %s\n", var_name);
        }
        
        else if (strncmp(input, "run ", 4) == 0) {
            char* command_to_run = input + 4;
            command_to_run[strcspn(command_to_run, "\n")] = 0;
            execute_command(command_to_run);
        } 

        else if (strncmp(input, "\\l ", 3) == 0) {
            char* device = input + 3;
            device[strcspn(device, "\n")] = 0;
            is_bootable_device(device); // Проверяем, является ли диск загрузочным
        }
        
        else if (strncmp(input, "\\mem ", 5) == 0) {
            char* proc_id = input + 5;
            proc_id[strcspn(proc_id, "\n")] = 0;
            dump_memory(proc_id); // Дамп памяти процесса
        }

        else if (strcmp(input, "\\cron\n") == 0) {
            create_vfs(); // Создаем VFS и выводим задачи планировщика
        }
        
        else {
            printf("The command isn't found!\n");
        }
    }

    fclose(history_file);
    return 0;
}
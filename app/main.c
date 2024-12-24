#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdbool.h>

void save_history(char*command) {
  FILE *file = fopen("history-txt", "a");
  if(file == NULL){
    perror("Failed to open");
    return;
  }
  fprintf(file, "%s\n", command);
  fclose(file);
}

int isNotEmpty(char *str) {
    for (size_t i = 0; i < strlen(str); i++) {
        // Прохожу по всей строке и проверяю каждый символ на то, является ли он пробелом с помощью функции isspace()
        if (!isspace(str[i])) {
            return 1;
        }
    }
    return 0;
}

void executeCommand(const char* command) { 
    pid_t pid = fork();
    if (pid == 0) {  // Дочерний процесс
        execlp(command, command, (char*)NULL);
        perror("execlp failed");
        exit(1);
    } else if (pid < 0) {
        perror("fork failed");
    } else {
        wait(NULL); // Ожидание завершения дочернего процесса
    }
}


void handle_sighup() {
    printf("Configuration reloaded\n\n");
    printf("Enter string> ");
    fflush(stdout); // Необходимо для немедленного вывода на консоль
}

int device(char*deviceName) {
    // Формируем полный путь к устройству
    char devicePath[256];
    snprintf(devicePath, sizeof(devicePath),"/dev/%s", deviceName);

    // Открываем устройство для чтения
    int fd = open(devicePath, O_RDONLY);
    if (fd == -1) {
        perror("Ошибка открытия устройства"); 
        return 1;
    }

    // Читаем первые 512 байт (первый сектор)
    unsigned char sector[512];
    ssize_t bytesRead = read(fd, sector, 512);
    if (bytesRead == -1) {
        perror("Ошибка чтения сектора");
        close(fd);
        return 1;
    } else if (bytesRead < 512) {
        fprintf(stderr, "Не удалось прочитать весь сектор.\n");
        close(fd);
        return 1;
    }

    // Извлекаем сигнатуру из первых двух байтов
    unsigned short signature;
    signature = (sector[510] << 8) | sector[511]; // 510 и 511 - байты сигнатуры

    // Проверяем сигнатуру
    // Запуск через sudo ./main, посмотреть диски df -h, выбор диска \l nvme0n1p1
    if (signature == 0x55AA) {
        printf("Устройство %s является загрузочным (сигнатура 0x55AA).\n\n", deviceName);
    } else {
        printf("Устройство %s не является загрузочным\n\n", deviceName);
    }

    // Закрываем устройство
    close(fd);
    return 0;
}

//12 по 'mem <procid>' получить дамп памяти процесса
bool appendToFile(char* path1, char* path2) {
    FILE *f1 = fopen(path1, "a");
    FILE *f2 = fopen(path2, "r");
    if (!f1 || !f2) {
        printf("Error while reading file %s\n", path2);
        return false;
    }
    char buf[256];

    while (fgets(buf, 256, f2) != NULL) {
        fputs(buf, f1);
    }
    fclose(f1);
    fclose(f2);
    return true;
}

void makeDump(DIR* dir, char* path) {
    FILE* res = fopen("res.txt", "w+");
    fclose(res);
    struct dirent* ent;
    char* file_path;
    while ((ent = readdir(dir)) != NULL) {

        asprintf(&file_path, "%s/%s", path, ent->d_name); // asprintf работает
        if(!appendToFile("res.txt", file_path)) {
            return;
        }
    }
    printf("Dump completed!\n");
}



int main() {
  // signal регистрирует функцию handle_sighup в качестве обработчика сигнала SIGHUP
  signal(1, handle_sighup); // kill -1 <pid>
  char input[300];
  
  while(1) {
    save_history(input);
    printf("Enter string> ");
    // Этот фрагмент кода предназначен для безопасного считывания строк из стандартного ввода. Он проверяет на ошибку чтения (fgets вернул NULL) и, если ошибка произошла, корректно завершает работу (или выходит из цикла) с сообщением "Exit". Использование sizeof(input) защищает программу от переполнения буфера, что очень важно с точки зрения безопасности.
    if(fgets(input, sizeof(input), stdin) == NULL) {
      printf("Exit\n"); 
      break;
    }

    // strcspn - находит количество символов в начале первой строки, которые не присутствуют во второй
    input[strcspn(input, "\n")] = 0;

    if(strcmp(input, "exit") == 0 || strcmp(input, "\\q") == 0) {
      printf("You have entered an output\n");
      break;
    }

    // strncmp - сравнивает первые n символов двух строк
    if (strncmp(input, "echo", 4) == 0) {
      if(strncmp(input, "echo ", 5) == 0){
        printf("%s\n", input+5); // %s - спецификатор формата для вывода строки, т.е принт будет искать указатель на строку и выведет ее
        printf("\n");
        continue;
      }
      else {
        printf("Incorrect using echo command!\n"); // %s - спецификатор формата для вывода строки, т.е принт будет искать указатель на строку и выведет ее
        printf("\n");
        continue;
      }
    }

    // strcmp - сравнивает 2 строки, если равны то возвращает 0
    if(!isNotEmpty(input)){
      printf("Enter correct command (command is empty)\n\n");
      continue;
    }

    if(strncmp(input, "\\e ", 3) == 0) {
      char* varName = input+3;
      varName[strcmp(varName, "\n")] = 0;
      // getenv() - функция для получения переменной окружения
      char* varValue = getenv(varName);
      if(varValue == NULL) {
        printf("Var not found: %s\n", varName);
      }
      else {
        printf("%s\n", varValue);
      }
      printf("\n");
      continue;
    }

    if (strncmp(input, "run ", 4) == 0) {
        char* comRun = input + 4;
        comRun[strcspn(comRun, "\n")] = 0;
        executeCommand(comRun);
        printf("\n");
        continue;
    } 


    if (strncmp(input, "\\l ", 3) == 0) {
        char* deviceName = input + 3;
        deviceName[strcspn(deviceName, "\n")] = 0;
        device(deviceName); // Проверяем, является ли диск загрузочным
        continue;
    }

    if (strncmp(input, "\\proc ", 6) == 0) { // sudo ./main вызываем
        char* path;
        asprintf(&path, "/proc/%s/map_files", input+6);

        DIR* dir = opendir(path);
        if (dir) {
            makeDump(dir, path);
        }
        else {
            printf("Process not found\n");
        }
        continue;
    }

    printf("Your string> %s\n\n", input);
  }


  return 0;
}


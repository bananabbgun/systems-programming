#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "hw2.h"

#define ERR_EXIT(s) perror(s), exit(errno);

/*
If you need help from TAs,
please remember :
0. Show your efforts
    0.1 Fully understand course materials
    0.2 Read the spec thoroughly, including frequently updated FAQ section
    0.3 Use online resources
    0.4 Ask your friends while avoiding plagiarism, they might be able to understand you better, since the TAs know the solution, 
        they might not understand what you're trying to do as quickly as someone who is also doing this homework.
1. be respectful
2. the quality of your question will directly impact the value of the response you get.
3. think about what you want from your question, what is the response you expect to get
4. what do you want the TA to help you with. 
    4.0 Unrelated to Homework (wsl, workstation, systems configuration)
    4.1 Debug
    4.2 Logic evaluation (we may answer doable yes or no, but not always correct or incorrect, as it might be giving out the solution)
    4.3 Spec details inquiry
    4.4 Testcase possibility
5. If the solution to answering your question requires the TA to look at your code, you probably shouldn't ask it.
6. We CANNOT tell you the answer, but we can tell you how your current effort may approach it.
7. If you come with nothing, we cannot help you with anything.
*/

// somethings I recommend leaving here, but you may delete as you please
static char root[MAX_FRIEND_INFO_LEN] = "Not_Tako";     // root of tree
static char friend_info[MAX_FRIEND_INFO_LEN];   // current process info
static char friend_name[MAX_FRIEND_NAME_LEN];   // current process name
static int friend_value;    // current process value
static int friend_level;    // 添加level信息
FILE* read_fp = NULL;
static friend children[MAX_CHILDREN];
static int child_count = 0;


// Is Root of tree
static inline bool is_Not_Tako() {
    return (strcmp(friend_name, root) == 0);
}

// a bunch of prints for you
void print_direct_meet(char *friend_name) {
    fprintf(stdout, "Not_Tako has met %s by himself\n", friend_name);
}

void print_indirect_meet(char *parent_friend_name, char *child_friend_name) {
    fprintf(stdout, "Not_Tako has met %s through %s\n", child_friend_name, parent_friend_name);
}

void print_fail_meet(char *parent_friend_name, char *child_friend_name) {
    fprintf(stdout, "Not_Tako does not know %s to meet %s\n", parent_friend_name, child_friend_name);
}

void print_fail_check(char *parent_friend_name){
    fprintf(stdout, "Not_Tako has checked, he doesn't know %s\n", parent_friend_name);
}

void print_success_adopt(char *parent_friend_name, char *child_friend_name) {
    fprintf(stdout, "%s has adopted %s\n", parent_friend_name, child_friend_name);
}

void print_fail_adopt(char *parent_friend_name, char *child_friend_name) {
    fprintf(stdout, "%s is a descendant of %s\n", parent_friend_name, child_friend_name);
}

void print_compare_gtr(char *friend_name){
    fprintf(stdout, "Not_Tako is still friends with %s\n", friend_name);
}

void print_compare_leq(char *friend_name){
    fprintf(stdout, "%s is dead to Not_Tako\n", friend_name);
}

void print_final_graduate(){
    fprintf(stdout, "Congratulations! You've finished Not_Tako's annoying tasks!\n");
}

/* terminate child pseudo code
void clean_child(){
    close(child read_fd);
    close(child write_fd);
    call wait() or waitpid() to reap child; // this is blocking
}

*/
// 清理子進程的函數
void clean_child(int child_index) {
    // 關閉對應的pipe
    close(children[child_index].read_fd);
    close(children[child_index].write_fd);
    
    // 等待子進程結束
    int status;
    waitpid(children[child_index].pid, &status, 0);
}

// 清理所有子進程
void clean_all_children() {
    for(int i = 0; i < child_count; i++) {
        clean_child(i);
    }
    child_count = 0;
}

/* remember read and write may not be fully transmitted in HW1?
void fully_write(int write_fd, void *write_buf, int write_len);

void fully_read(int read_fd, void *read_buf, int read_len);

please do above 2 functions to save some time
*/
void fully_write(int fd, void *buf, size_t count) {
    size_t written = 0;
    while (written < count) {
        ssize_t result = write(fd, buf + written, count - written);
        if (result < 0) ERR_EXIT("write");
        written += result;
    }
}

void fully_read(int fd, void *buf, size_t count) {
    size_t read_bytes = 0;
    while (read_bytes < count) {
        ssize_t result = read(fd, buf + read_bytes, count - read_bytes);
        if (result <= 0) ERR_EXIT("read");
        read_bytes += result;
    }
}

// 從名字中提取value
int extract_value(const char *info) {
    char *underscore = strrchr(info, '_');
    if (underscore) {
        return atoi(underscore + 1);
    }
    return 0;
}

// 從名字中提取name
void extract_name(const char *info, char *name) {
    const char *underscore = strrchr(info, '_');
    if (underscore) {
        size_t len = underscore - info;
        strncpy(name, info, len);
        name[len] = '\0';
    }
}

/*#define MAX_LEVEL 7
#define MAX_LINE_LEN 1024  // 每行的最大長度

typedef struct {
    char lines[MAX_LEVEL][MAX_LINE_LEN];  // 每層一行字符串
    int max_level;                         // 記錄最大層數
} tree_output;*/

// 處理Meet命令
int handle_meet(const char *cmd) {
    char parent_name[MAX_FRIEND_NAME_LEN];
    char child_info[MAX_FRIEND_INFO_LEN];
    sscanf(cmd, "Meet %s %s", parent_name, child_info);
    /*printf("Received command: '%s'\n", cmd);
    printf("parent_name: '%s'\n", parent_name);
    printf("Parsed child_info: '%s'\n", child_info);
    printf("friend_name: '%s'\n", friend_name);*/
    // 如果我是目標父節點
    if (strcmp(friend_name, parent_name) == 0) {
        if (child_count >= MAX_CHILDREN) {
            return -1; // 超過最大子節點數
        }

        // 創建兩個pipe
        int parent_to_child[2], child_to_parent[2];
        if (pipe(parent_to_child) < 0 || pipe(child_to_parent) < 0) {
            ERR_EXIT("pipe");
        }

        // 在fork之前，計算子節點的level
        int child_level = friend_level + 1;

        // fork子進程
        pid_t pid = fork();
        if (pid < 0) {
            ERR_EXIT("fork");
        }

        if (pid == 0) { // 子進程
            // 關閉所有繼承的file descriptors
            for(int i = 0; i < child_count; i++) {
                close(children[i].read_fd);
                close(children[i].write_fd);
            }
            // 關閉不需要的pipe端
            close(parent_to_child[1]);
            close(child_to_parent[0]);
            
            // 重定向fd 3和4，並添加錯誤檢查
            if(dup2(parent_to_child[0], PARENT_READ_FD) < 0) {
                fprintf(stderr, "Failed to dup2 read fd: %s\n", strerror(errno));
                _exit(1);
            }
            if(dup2(child_to_parent[1], PARENT_WRITE_FD) < 0) {
                fprintf(stderr, "Failed to dup2 write fd: %s\n", strerror(errno));
                _exit(1);
            }
            
            // 驗證fd是否正確設置
            if (fcntl(PARENT_READ_FD, F_GETFD) == -1) {
                fprintf(stderr, "PARENT_READ_FD not properly set up: %s\n", strerror(errno));
                _exit(1);
            }
            /*// 重定向fd 3和4
            dup2(parent_to_child[0], PARENT_READ_FD);
            dup2(child_to_parent[1], PARENT_WRITE_FD);*/
            
            /*// 設置 FD_CLOEXEC 標誌為 0，這樣 exec 後 fd 就不會被關閉
            fcntl(PARENT_READ_FD, F_SETFD, 0);
            fcntl(PARENT_WRITE_FD, F_SETFD, 0);*/

            // 關閉原始的fd
            close(parent_to_child[0]);
            close(child_to_parent[1]);

            // 創建環境變量
            char level_str[16];
            snprintf(level_str, sizeof(level_str), "%d", child_level);
            setenv("FRIEND_LEVEL", level_str, 1);  // 設置環境變量

            // exec新的friend進程
            char *args[] = {"./friend", child_info, NULL};
            //printf("exec child_info: '%s'\n", child_info);
            execvp(args[0], args);
            ERR_EXIT("execvp");
        }

        // 父進程
        // 關閉不需要的pipe端
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        
        // 保存子節點信息
        friend *child = &children[child_count];
        child->pid = pid;
        child->read_fd = child_to_parent[0];
        child->write_fd = parent_to_child[1];
        strncpy(child->info, child_info, MAX_FRIEND_INFO_LEN);
        extract_name(child_info, child->name);
        child->value = extract_value(child_info);
        child->level = child_level;  // 保存level信息
        
        child_count++;
        //printf("%s have %d childs\n", friend_name, child_count);

        // 根據是否為根節點輸出不同信息
        if (is_Not_Tako()) {
            print_direct_meet(child->name);
            //printf("level = %d\n", child->level);
        } else {
            print_indirect_meet(friend_name, child->name);
            //printf("%s level = %d\n",child->name, child->level);
            // 向父節點回報成功
            char result = 1;
            write(PARENT_WRITE_FD, &result, 1);
            //printf("%s Success!!\n", friend_name);
        }

        return 0;
    } 
    
    // 如果我不是目標父節點，遞歸傳遞給子節點
    bool found = false;
    for (int i = 0; i < child_count; i++) {
        //printf("[DEBUG] %s trying to pass command to child %s\n", friend_name, children[i].name);
    
        // 傳遞Meet命令給子節點，確保包含換行符
        char cmd_buf[MAX_CMD_LEN];
        snprintf(cmd_buf, sizeof(cmd_buf), "%s\n", cmd);
        
        // 使用write而不是fully_write來發送命令
        ssize_t write_len = strlen(cmd_buf);
        if (write(children[i].write_fd, cmd_buf, write_len) != write_len) {
            fprintf(stderr, "Failed to write to child\n");
            continue;
        }
        
        //printf("[DEBUG] %s waiting for response from child %s\n", friend_name, children[i].name);
        /*printf("child name %s\ncommand %s\n", children[i].info, cmd);
        // 傳遞Meet命令給子節點
        char cmd_buf[MAX_CMD_LEN];
        
        //strcpy(cmd_buf, cmd);  // 複製命令到緩衝區
        snprintf(cmd_buf, sizeof(cmd_buf), "%s\n", cmd);  // 在命令後添加換行符
        write(children[i].write_fd, cmd_buf, strlen(cmd_buf) + 1);*/
        
        // 讀取子節點的結果
        char result;
        read(children[i].read_fd, &result, 1);
        if (result == 1) {
            found = true;
            // 如果找到了，且我不是根節點，就向我的父節點回報成功
            if (!is_Not_Tako()) {
                write(PARENT_WRITE_FD, &result, 1);
            }
            //printf("%s Success!!\n", friend_name);
            break;
        }
    }
    if (!found) {
        if (is_Not_Tako()) {
            char child_name[MAX_FRIEND_NAME_LEN];
            extract_name(child_info, child_name);  // 先提取child的name
            print_fail_meet(parent_name, child_name);  // 使用name而不是info
        } else {
            // 向父節點回報失敗
            char result = 0;
            write(PARENT_WRITE_FD, &result, 1);
            //printf("[DEBUG] %s reporting failure to parent\n", friend_name);
        }
    }
    return found ? 0 : -1;
}

// 初始化tree_info
/*void init_tree_info(tree_info *tree) {
    memset(tree, 0, sizeof(tree_info));
    tree->levels = 0;
}

// 添加節點信息到對應層級
void add_to_level(tree_info *tree, const char *node_info, int level) {
    // 如果該層已有節點，添加空格
    if (strlen(tree->nodes[level]) > 0) {
        strcat(tree->nodes[level], " ");
    }
    strcat(tree->nodes[level], node_info);
    
    // 更新最大層級
    if (level + 1 > tree->levels) {
        tree->levels = level + 1;
    }
}*/

int handle_check(const char *cmd) {
    char target_name[MAX_FRIEND_NAME_LEN];
    sscanf(cmd, "Check %s", target_name);
    
    // 如果我是目標節點
    if (strcmp(friend_name, target_name) == 0) {
        // 先印出自己
        if (is_Not_Tako()) {
            printf("Not_Tako\n");
        } else {
            printf("%s\n", friend_info);
        }
        
        // 印出直接子節點
        for (int i = 0; i < child_count; i++) {
            if (i > 0) printf(" ");
            printf("%s", children[i].info);
        }
        if (child_count > 0) printf("\n");
        
        // 開始處理更深層的節點 (level+2 到 level+6)
        for (int current_level = friend_level + 2; current_level <= friend_level + 6; current_level++) {
            bool has_response = false;
            // 發送print指令給所有子節點
            for (int i = 0; i < child_count; i++) {
                char print_cmd[MAX_CMD_LEN];
                snprintf(print_cmd, MAX_CMD_LEN, "print %d %d\n", current_level, (i == 0));
                fully_write(children[i].write_fd, print_cmd, strlen(print_cmd));
                
                // 讀取回應
                char response = '0';
                
                fully_read(children[i].read_fd, &response, 1);
                if (response == 1) {
                    has_response = true;
                }
            }
            // 只有在有回應時才換行
            if (has_response) printf("\n");
        }
        
        // 如果不是root，向父節點報告成功
        if (!is_Not_Tako()) {
            char result = 1;
            write(PARENT_WRITE_FD, &result, 1);
        }
        return 0;
    }
    // 如果不是目標節點，向下遞歸查找
    else {
        bool found = false;
        for (int i = 0; i < child_count; i++) {
            char cmd_buf[MAX_CMD_LEN];
            snprintf(cmd_buf, MAX_CMD_LEN, "%s\n", cmd);
            fully_write(children[i].write_fd, cmd_buf, strlen(cmd_buf));
            
            char result;
            fully_read(children[i].read_fd, &result, 1);
            if (result == 1) {
                found = true;
                if (!is_Not_Tako()) {
                    write(PARENT_WRITE_FD, &result, 1);
                }
                break;
            }
        }
        
        // 如果是root且沒找到，輸出錯誤訊息
        if (!found) {
            if (is_Not_Tako()) {
                print_fail_check(target_name);
            } else {
                // 如果不是root，向父節點報告失敗
                char result = 0;
                write(PARENT_WRITE_FD, &result, 1);
            }
        }
        
        return found ? 0 : -1;
    }
}

int main(int argc, char *argv[]) {
    // Hi! Welcome to SP Homework 2, I hope you have fun
    pid_t process_pid = getpid(); // you might need this when using fork()
    if (argc != 2) {
        fprintf(stderr, "Usage: ./friend [friend_info]\n");
        return 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0); // prevent buffered I/O, equivalent to fflush() after each stdout, study this as you may need to do it for other friends against their parents
    
    // put argument one into friend_info
    strncpy(friend_info, argv[1], MAX_FRIEND_INFO_LEN);
    
    if(strcmp(argv[1], root) == 0){
        // is Not_Tako
        strncpy(friend_name, friend_info,MAX_FRIEND_NAME_LEN);      // put name into friend_name
        friend_name[MAX_FRIEND_NAME_LEN - 1] = '\0';        // in case strcmp messes with you
        read_fp = stdin;        // takes commands from stdin
        friend_value = 100;     // Not_Tako adopting nodes will not mod their values
        friend_level = 0;  // root的level為0
    }
    else{
        extract_name(friend_info, friend_name);
        friend_value = extract_value(friend_info);

        // 從環境變量獲取level
        char *level_str = getenv("FRIEND_LEVEL");
        friend_level = level_str ? atoi(level_str) : 1; // 如果沒有環境變量，默認為1

        //printf("%s %d\n", friend_name,friend_value);
        // is other friends
        // extract name and value from info
        // where do you read from?
        // anything else you have to do before you start taking commands?

        // 先檢查fd是否有效
        if (fcntl(PARENT_READ_FD, F_GETFD) == -1) {
            fprintf(stderr, "Invalid PARENT_READ_FD before fdopen: %s\n", strerror(errno));
            _exit(1);
        }
        // 將標準文件描述符(3,4)轉換為FILE*，用於讀取命令
        read_fp = fdopen(PARENT_READ_FD, "r");
        if (read_fp == NULL) {
            ERR_EXIT("fdopen");
        }

        // 關閉不需要的標準輸入
        // 因為我們只從父節點的pipe讀取命令
        close(STDIN_FILENO);
    }

    //TODO:
    /* you may follow SOP if you wish, but it is not guaranteed to consider every possible outcome

    1. read from parent/stdin
    2. determine what the command is (Meet, Check, Adopt, Graduate, Compare(bonus)), I recommend using strcmp() and/or char check
    3. find out who should execute the command (extract information received)
    4. execute the command or tell the requested friend to execute the command
        4.1 command passing may be required here
    5. after previous command is done, repeat step 1.
    */

    // Hint: do not return before receiving the command "Graduate"
    // please keep in mind that every process runs this exact same program, so think of all the possible cases and implement them

    /* pseudo code
    if(Meet){
        create array[2]
        make pipe
        use fork.
            Hint: remember to fully understand how fork works, what it copies or doesn't
        check if you are parent or child
        as parent or child, think about what you do next.
            Hint: child needs to run this program again
    }
    else if(Check){
        obtain the info of this subtree, what are their info?
        distribute the info into levels 1 to 7 (refer to Additional Specifications: subtree level <= 7)
        use above distribution to print out level by level
            Q: why do above? can you make each process print itself?
            Hint: we can only print line by line, is DFS or BFS better in this case?
    }
    else if(Graduate){
        perform Check
        terminate the entire subtree
            Q1: what resources have to be cleaned up and why?
            Hint: Check pseudo code above
            Q2: what commands needs to be executed? what are their orders to avoid deadlock or infinite blocking?
            A: (tell child to die, reap child, tell parent you're dead, return (die))
    }
    else if(Adopt){
        remember to make fifo
        obtain the info of child node subtree, what are their info?
            Q: look at the info you got, how do you know where they are in the subtree?
            Hint: Think about how to recreate the subtree to design your info format
        A. terminate the entire child node subtree
        B. send the info through FIFO to parent node
            Q: why FIFO? will usin pipe here work? why of why not?
            Hint: Think about time efficiency, and message length
        C. parent node recreate the child node subtree with the obtained info
            Q: which of A, B and C should be done first? does parent child position in the tree matter?
            Hint: when does blocking occur when using FIFO?(mkfifo, open, read, write, unlink)
        please remember to mod the values of the subtree, you may use bruteforce methods to do this part (I did)
        also remember to print the output
    }
    else if(full_cmd[1] == 'o'){
        Bonus has no hints :D
    }
    else{
        there's an error, we only have valid commmands in the test cases
        fprintf(stderr, "%s received error input : %s\n", friend_name, full_cmd); // use this to print out what you received
    }
    */

   char cmd[MAX_CMD_LEN];
    while (fgets(cmd, MAX_CMD_LEN, read_fp) != NULL) {
        //printf("HI I'm %s\n", friend_name);
        cmd[strcspn(cmd, "\n")] = 0; // 移除換行符
        //printf("Main command: '%s'\n", cmd);
        
        if (strncmp(cmd, "Meet", 4) == 0) {
            //printf("MEET\n");
            handle_meet(cmd);
            
        }
        else if (strncmp(cmd, "Check", 5) == 0) {
            handle_check(cmd);
        }
        else if (strncmp(cmd, "print", 5) == 0) {
            // 處理print指令
            int target_level, is_first;
            sscanf(cmd, "print %d %d", &target_level, &is_first);
            
            // 如果我的children所在的level就是目標level
            if (friend_level + 1 == target_level) {
                // is_first現在代表的是整層是否為第一個輸出
                for (int i = 0; i < child_count; i++) {
                    if (!is_first) {  // 如果不是這層的第一個輸出，加空格
                        printf(" ");
                    }
                    printf("%s", children[i].info);
                    is_first = false;  // 第一個輸出之後，後面都要加空格
                }
                char result = (child_count > 0) ? 1 : 0;
                write(PARENT_WRITE_FD, &result, 1);
            }
            // 如果還沒到目標level，繼續往下傳
            else {
                bool any_success = false;
                for (int i = 0; i < child_count; i++) {
                    char print_cmd[MAX_CMD_LEN];
                    // 傳遞目前是否還是這層的第一個輸出
                    snprintf(print_cmd, MAX_CMD_LEN, "print %d %d\n", 
                            target_level, (!any_success && is_first));
                    fully_write(children[i].write_fd, print_cmd, strlen(print_cmd));
                    
                    char response;
                    fully_read(children[i].read_fd, &response, 1);
                    if (response == 1) {
                        any_success = true;
                        is_first = false;  // 如果有成功輸出，之後的都不是第一個了
                    }
                }
                
                char result = any_success ? 1 : 0;
                write(PARENT_WRITE_FD, &result, 1);
            }
        }
        // ... 其他命令的處理
    }

    /*if (read_fp != stdin) {
        fclose(read_fp);
    }*/
   // final print, please leave this in, it may bepart of the test case output
    /*if(is_Not_Tako()){
        print_final_graduate();
    }*/
    _exit(0);
}
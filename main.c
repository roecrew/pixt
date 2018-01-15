//
//  main.c
//  
//
//  Created by fairy-slipper on 1/13/18.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <limits.h>

#define INIT 1
#define UPDATE 2

#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_RESET   "\x1b[0m"
#define ANSI_BLINK   "\x1b[5m"


const int MAX_CONSOLE_TEXT = 10000;

static volatile int appRunning = 1;
static struct winsize winsize;
static int cooked = 0;
static int editing;
static int readyToRender = 1;

static char enteredChar = 0;
static char *consoleText;
static int cursorPosition = 2;

static int editingCursorPositionX = 1;
static int editingCursorPositionY = 1;

static FILE *fp = NULL;
static int fpOffset = 0;
static FILE *fpClone = NULL;

static char *newFileString;
static int newFileStrOffset = -1;

struct dirent *pDirent;
struct dirent *selectedDirent;
static DIR *pDir;
static char cwd[PATH_MAX];
static char *currCloneFilePath;
static int directoryWidth = 0;
static int directoryHeight = 0;
static int contentCount;
static int numOfPagesNav = 0;
static int currPage = 0;

void appendChars(char subject[], const char insert[], int pos) {
    char buf[1000000] = {};
    strncpy(buf, subject, pos);
    int len = strlen(buf);
    strcpy(buf+len, insert);
    len += strlen(insert);
    strcpy(buf+len, subject+pos);
    strcpy(subject, buf);
}

void removeChar(char subject[], int pos) {
    memmove(&subject[pos], &subject[pos + 1], strlen(subject) - pos);
}

void analyzeDirectory() {
    rewinddir(pDir);
    directoryWidth = 0;
    directoryHeight = 0;
    while((pDirent = readdir(pDir)) != NULL) {
        if(directoryWidth < strlen(pDirent->d_name)) {
            directoryWidth = strlen(pDirent->d_name);
        }
        ++directoryHeight;
    }
    rewinddir(pDir);
    
    numOfPagesNav = (directoryHeight/winsize.ws_row) + 1;
}

void refreshDisplay(int type) {
    contentCount = 0;
    readyToRender = 0;
    analyzeDirectory();
    
    if(type != INIT) {
        printf("\033[%i;%iH", 1, 1);
        rewind(stdout);
        if(!cooked) {
            printf("\r");
        }
    }
    
    int pageOffset = 0;
    while((pDirent = readdir(pDir)) != NULL) {
        if((numOfPagesNav - 1) == 0 || currPage == 0) {
            rewinddir(pDir);
            break;
        } else if(pageOffset >= currPage * winsize.ws_row) {
            break;
        }
        ++pageOffset;
    }
    
    int rowOffset = 0;
    if(cooked) {
        rowOffset = 2;
    }
    
    //    if(fp != NULL) {
    //        rewind(fp);
    //    }
    
    if(!editing) {
        int newLineFlag = 0;
        for(int h=0; h<winsize.ws_row - rowOffset; h++) {
            pDirent = readdir(pDir);
            
            int contentLen;
            if(pDirent != NULL) {
                ++contentCount;
                contentLen = strlen(pDirent->d_name);
            }
            int nonPrintable = 0;
            int tabOccured = 0;
            if(newLineFlag > 0) {--newLineFlag;}
            for(int w=0; w<winsize.ws_col; w++) {
                if(w == 0) {
                    if(cursorPosition == h) {
                        printf("%s>%s%s", ANSI_RESET, ANSI_RESET, ANSI_COLOR_GREEN);
                        selectedDirent = pDirent;
                    } else {
                        printf(" ");
                    }
                } else {
                    if(pDirent != NULL && contentLen > w - 1 && pDirent->d_name[w - 1] != '\n' && pDirent->d_name[w - 1] != '\r') {
                        printf("%c", pDirent->d_name[w - 1]);
                    } else if(directoryWidth + 2 == w) {
                        printf("|");
                    } else if(directoryWidth + 3 == w) {
                        printf(" ");
                    } else if(fp != NULL && w > directoryWidth + 3) {
                        int val;
                        fpOffset = ftell(fp);
                        if(tabOccured || newLineFlag) {
                            printf(" ");
                        } else if(!nonPrintable) {
                            val = fgetc(fp);
                            if((31 < val && val < 127) || val == 9) {
                                if(val == 9) {
                                    tabOccured = 4;
                                    printf(" ");
                                } else {
                                    printf("%c", val);
                                }
                            } else {
                                nonPrintable = 1;
                                printf(" ");
                            }
                        } else {
                            printf(" ");
                            val = fgetc(fp);
                            if(val == '\n') {
                                newLineFlag = 2;
                            } else if((31 < val && val < 127) || val == 9) {
                                fseek(fp, -1, SEEK_CUR);
                            }
                        }
                    } else {
                        printf(" ");
                    }
                    if(tabOccured > 0) { --tabOccured;}
                }
            }
        }
        if(cooked) {
            printf("\n");
        }
    } else {
        int i = 0;
        for(int h=0; h<winsize.ws_row - rowOffset; h++) {
            int newline = 0;
            int tabOccured = 0;
            for(int w=0; w<winsize.ws_col; w++) {
                int val;
                fpOffset = ftell(fp);
                if(tabOccured || newline) {
                    printf(" ");
                } else {
                    if((val = newFileString[i++]) != '\0') {
                        if(val == 9) {
                            tabOccured = 4;
                            printf(" ");
                        } else if(val == '\n') {
                            newline = 1;
                            printf(" ");
                        } else {
                            printf("%c", val);
                        }
                    } else {
                        printf(" ");
                    }
                }
                if(tabOccured > 0) { --tabOccured;}
            }
        }
    }
    rewind(stdout);
    readyToRender = 1;
}

void upLine() {
    int xOffset = 0;
    int i = 1;
    int cnt = 1;
    while(newFileString[newFileStrOffset - i] != 10 && newFileStrOffset - i > -1) {
        if(newFileString[newFileStrOffset - i] == 9) {
            cnt += 4;
            i++;
        } else {
            cnt++;
            i++;
        }
    }
    editingCursorPositionX = cnt;
}

void rightArrow(int enteredValue) {
    if(!editing) {
        char tmp[sizeof(selectedDirent->d_name)];
        char cwdTmp[sizeof(cwd)];
        strcpy(tmp, selectedDirent->d_name);
        strcpy(cwdTmp, cwd);
        strcat(cwdTmp, "/");
        strcat(cwdTmp, tmp);
        closedir(pDir);
        pDir = opendir(cwdTmp);
        if (pDir != NULL) {
            strcpy(cwd, cwdTmp);
            cursorPosition = 2;
            currPage = 0;
        } else {
            pDir = opendir(cwd);
            fclose(fp);
            char filePath[sizeof(cwd)];
            memset(filePath, 0, sizeof(filePath));
            strcpy(filePath, cwd);
            strcat(filePath, "/");
            strcat(filePath, selectedDirent->d_name);
            strcpy(currCloneFilePath, filePath);
            strcat(currCloneFilePath, "-tmp");
            fp = fopen(filePath, "r");
        }
        refreshDisplay(UPDATE);
    } else {
        if(newFileString[newFileStrOffset+1] == '\0') {
            
        } else {
            ++newFileStrOffset;
            if(newFileString[newFileStrOffset] == 10 || enteredValue == 10) {
                editingCursorPositionX = 1;
                ++editingCursorPositionY;
            } else if(newFileString[newFileStrOffset] == 9) {
                editingCursorPositionX += 4;
            } else {
                ++editingCursorPositionX;
            }
            //printf("%c\n\n", newFileString[newFileStrOffset]);
            printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
        }
    }
}

void leftArrow(enteredValue) {
    if(!editing) {
        int len = strlen(cwd);
        if(strcmp(cwd, "/")) {
            for(int i=len-1; i>=0; i--) {
                if (cwd[i] == '/') {
                    cwd[i] = '\0';
                    if(cwd[0] == '\0') {
                        cwd[0] = '/';
                        cwd[1] = '\0';
                    }
                    break;
                }
            }
        }
        closedir (pDir);
        pDir = opendir(cwd);
        if (pDir == NULL) {
            printf ("Cannot open directory\n");
        }
        cursorPosition = 2;
        currPage = 0;
        refreshDisplay(UPDATE);
    } else {
        if(newFileStrOffset - 1 < -1) {
            
        } else {
            if((newFileString[newFileStrOffset] == 10 && enteredValue != 127) || enteredValue == 600) {
                --editingCursorPositionY;
                upLine();
            } else if(newFileString[newFileStrOffset] == 9 || enteredValue == 700) {
                editingCursorPositionX -= 4;
            } else {
                --editingCursorPositionX;
            }
            --newFileStrOffset;
            //printf("%c", newFileString[newFileStrOffset]);
            printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
        }
    }
}

int main(int argc, char **argv) {
    
    printf(ANSI_COLOR_GREEN);
    system("/bin/stty raw");
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize);
    
    consoleText = (char *)malloc(MAX_CONSOLE_TEXT*sizeof(char));
    currCloneFilePath = (char *)malloc(MAX_CONSOLE_TEXT*sizeof(char));
    newFileString = (char *)malloc(1000000*sizeof(char));
    
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
    }
    
    pDir = opendir (cwd);
    if (pDir == NULL) {
        printf ("Cannot open directory\n");
    }
    
    refreshDisplay(INIT);
    refreshDisplay(UPDATE);
    
    int lastEnteredChar = 0;
    int lastlastEnteredChar = 0;
    while ((enteredChar = fgetc(stdin)) != EOF && appRunning) {
        if(readyToRender) {
            //printf("%i\n", enteredChar);
            if((lastEnteredChar == 27 && enteredChar == 13) || (lastEnteredChar == 27 && enteredChar == 10)) { //end app
                appRunning = 0;
                break;
            }
            if(editing && enteredChar == 15) {
                editing = 0;
                fpClone = fopen(currCloneFilePath, "w+");
                int val;
                int i = 0;
                while((val = newFileString[i++]) != '\0') {
                    fputc(val, fpClone);
                }
                fclose(fpClone);
                refreshDisplay(UPDATE);
                continue;
            }
            if(editing && enteredChar == 24) {
                editing = 0;
                fclose(fpClone);
                remove(currCloneFilePath);
                refreshDisplay(UPDATE);
                continue;
            }
            if(enteredChar == 10 && cooked) {
                refreshDisplay(UPDATE);
            }
            if(enteredChar == 13 && !cooked && !editing) {
                if(fp != NULL) {
                    editing = 1;
                    
                    rewind(fp);
                    int val;
                    int i = 0;
                    while((val = fgetc(fp)) != EOF) {
                        if(val == 10 && newFileString[i-1] == 32) {
                            newFileString[i-1] = val;
                        } else {
                            newFileString[i++] = val;
                        }
                    }
                    newFileString[i] = '\0';
                    rewind(fp);
                    refreshDisplay(UPDATE);
                    printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                }
            } else if((lastEnteredChar == 14 && enteredChar == 13) || (cooked && lastEnteredChar == 14 && enteredChar == 10)) {
                if(cooked) {
                    system("/bin/stty raw");
                    cooked = 0;
                } else {
                    system ("/bin/stty cooked");
                    cooked = 1;
                }
                refreshDisplay(UPDATE);
                refreshDisplay(UPDATE);
            } else if(!cooked) {
                if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 65) { //up
                    if(!editing) {
                        if(fp != NULL) {
                            fclose(fp);
                        }
                        --cursorPosition;
                        if(cursorPosition < 0) {
                            cursorPosition = contentCount - 1;
                            --currPage;
                            if(currPage < 0) {
                                currPage = numOfPagesNav -1;
                            }
                        }
                        refreshDisplay(UPDATE);
                    } else {
                        if(editingCursorPositionY > 1) {
                            while(newFileStrOffset > -1 && newFileString[newFileStrOffset] != 10) {
                                --newFileStrOffset;
                            }
                            if(newFileString[newFileStrOffset] == '\n') {
                                --newFileStrOffset;
                            }

                            while(newFileStrOffset > -1 && newFileString[newFileStrOffset] != 10) {
                                --newFileStrOffset;
                            }
                            int i = 1;
                            int newOffset = 0;
                            while(i < editingCursorPositionX) {
                                if(newFileString[newFileStrOffset + i] == 10) {
                                    break;
                                } else if(newFileString[newFileStrOffset + i] == '\0') {
                                    break;
                                } else if(newFileString[newFileStrOffset + i] == 9) {
                                    i+=4;
                                    newOffset++;
                                } else {
                                    i++;
                                    newOffset++;
                                }
                            }
                            
                            newFileStrOffset += newOffset;
                            editingCursorPositionX = i;
                            --editingCursorPositionY;
                        }
                        printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                    }
                } else if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 66) { //down
                    if(!editing) {
                        if(fp != NULL) {
                            fclose(fp);
                        }
                        ++cursorPosition;
                        if(cursorPosition > contentCount - 1) {
                            cursorPosition = 0;
                            ++currPage;
                            if(currPage > numOfPagesNav - 1) {
                                currPage = 0;
                            }
                        }
                        refreshDisplay(UPDATE);
                    } else {
                        while(newFileString[newFileStrOffset] != '\n' && newFileString[newFileStrOffset] != '\0') {
                            ++newFileStrOffset;
                        }
                        if(newFileString[newFileStrOffset] == '\0') {
                            
                        } else {
                            if(newFileString[newFileStrOffset] == '\n') {
                                ++newFileStrOffset;
                            }
                            int i = 1;
                            ++editingCursorPositionY;
                            while (i < editingCursorPositionX && newFileString[newFileStrOffset] != '\0' && newFileString[newFileStrOffset] != '\n') {
                                if(newFileString[newFileStrOffset] == 9) {
                                    i += 3;
                                }
                                ++i;
                                ++newFileStrOffset;
                            }
                            --newFileStrOffset;
                            editingCursorPositionX = i;
                        }
                        printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                    }
                } else if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 67) { //right
                    rightArrow(-1);
                } else if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 68) { //left
                    leftArrow(-1);
                } else if(lastlastEnteredChar != 27 && lastEnteredChar != 27 && ((enteredChar > 31 && enteredChar < 127) || (enteredChar == 13))) {
                    if(enteredChar == 13) {enteredChar = '\n';}
                    char tmp[2];
                    tmp[0] = enteredChar;
                    tmp[1] = '\0';
                    appendChars(newFileString, tmp, newFileStrOffset+1);
                    refreshDisplay(UPDATE);
                    rightArrow(enteredChar);
                } else if(enteredChar == 127) {
                    if(newFileStrOffset - 1 > -2) {
                        
                        if(newFileString[newFileStrOffset] == 10) {
                            --editingCursorPositionY;
                            if(newFileString[newFileStrOffset - 1] == ' ') {
                                removeChar(newFileString, newFileStrOffset);
                                removeChar(newFileString, newFileStrOffset - 1);//needs work
                                ++editingCursorPositionX;
                            } else {
                                removeChar(newFileString, newFileStrOffset);
                            }
                            upLine();
                            --newFileStrOffset;
                            //++editingCursorPositionX;
                        } else if(newFileString[newFileStrOffset] == 9) {
                            editingCursorPositionX -= 4;
                            removeChar(newFileString, newFileStrOffset);
                            --newFileStrOffset;
                        } else {
                            --editingCursorPositionX;
                            removeChar(newFileString, newFileStrOffset);
                            --newFileStrOffset;
                        }
                        
                        
                        refreshDisplay(UPDATE);
                        
                        printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                    }
                }
            }
            lastlastEnteredChar = lastEnteredChar;
            lastEnteredChar = enteredChar;
        }
    }
    
    printf(ANSI_RESET);
    fclose(fp);
    fclose(fpClone);
    system ("/bin/stty cooked");
    closedir (pDir);
    free(consoleText);
    free(currCloneFilePath);
    
    return 0;
}

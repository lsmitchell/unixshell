#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <math.h>
#include <time.h>
#include <dirent.h>

using namespace std;

/**
*   OS1Shell 2.0 by Logan Mitchell
**/

//vars

const int BUFFER_MAX_SIZE = 65; //64 + '\n'
const int HISTORY_MAX_SIZE = 20;
const int DEFAULT_FILE_SYSTEM_SIZE = 10; //MB
const int DEFAULT_CLUSTER_SIZE = 8; //KB
int CLUSTER_SIZE_BYTES = -1;
int FILE_SYSTEM_SIZE_BYTES = -1;
int numClusters = -1; //useful to store for adjusting FAT
int MAX_FILES = -1; //use for determining limit on dir table array
int ROOT_DIR_INDEX = -1;
int FAT_INDEX = -1;
bool handledCtrl = false;
string fileSystemPath = ""; //full path to our FS to see when we're in it
FILE* fileSystem;
string currentPath = ""; //current cd path; starts as fileSystemPath

#define TYPE_FILE 0x00
#define TYPE_DIRECTORY 0xFF

struct fileInfo { char filename[112];  unsigned int index; unsigned int fileSize; unsigned int type; unsigned int creationDate;};

//Methods

//shell
void apocalypse();
void clearReadBufferOverflow();
void forkAndExecute(char* params[], bool waitForChild, string originalCommand);
void makeParamsFromTokens(vector<string> tokens, char* params[]);
void parseAndExecute(char inputBuffer[], int readChars);
void sendSelfSignal(int signalNum);
vector<string> tokenizeInput(char* input, int length);

//file system
void checkForFileSystem(string, string);
bool createNewFileSystem(string, string);
void setFileSystemPath(string);
void nullCluster(int);
void writeCluster(int clusterIndex, unsigned int* data, int writeBytes, int offset);
void writeCluster(int clusterIndex, int* data, int writeBytes, int offset);
void writeCluster(int clusterIndex, char* data, int writeBytes, int offset);
void writeCluster(int clusterIndex, fileInfo* data, int writeBytes, int offset);
void changeFATEntry(int,int);
void getBootRecord(unsigned int*);
int writeData(char* data, int, int, vector<int>*);
int writeData(fileInfo* data);
int findNextFreeCluster(vector<int>*);
int writeFile(char* filename, char* data, int dataLength, bool isDirectory);
int readFile(char* filename, char* fileContainer);
int readData(int clusterIndex, int fileSize, char* fileContainer);
int writeDirectoryTableClusters(fileInfo* data, int offset, int curDirTableIndex);
void writeDirectoryTable(fileInfo* dirTable);
int addDirectoryTableEntry(char* filename, unsigned int startCluster, unsigned int fileSizeBytes, unsigned int fileType, unsigned int createTime);
void writeBoot(unsigned int* data, int writeBytes);
int deleteFile(string fileName);
void getFAT(int*);
void getDirectoryTable(fileInfo* dirTableContainer);
void getFileData(char* FileContainer, int fileSize, FILE* filePointer);

//setup methods
void setupBootRecord();
void setupFAT();
void initializeDirTable(fileInfo* dirTable);
int requestClusterSize();
int requestFileSystemSize();

//command line functions
void commandCd(char** params, int numParams);
void commandLs(char** params, int numParams);
void commandTouch(char** params, int numParams);
void commandCp(char** params, int numParams);
void commandMv(char** params, int numParams);
void commandRm(char** params, int numParams);
void commandDf(char** params);
void commandCat(char** params, int numParams);

bool checkForLocalHandling(char** params, int numParams);

//printing
void printFAT(int, int);
void printDirectoryTable();
void printDirectoryNames(bool longVer);

//aux functions
string epochToDatetime(int epochTime);
void toCString(string inputString, char outputCstring[]);
bool filenameExists(char* name);
bool isDirectory(char* filePath);
bool pathExists(char* filePath);
bool isDirectory(string filePath);
bool pathExists(string filePath);
bool isLocalPath(char* filePath);
bool isValidLocalPath(char* localPath);
string getFileNameFromPath(char* pathName);
int getExternalFileSize(char* filePath);
int getLocalFileSize(char* filename);
bool isLastCharSlash(string filePath);












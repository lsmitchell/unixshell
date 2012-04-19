#include "os1shell.h"

/**
*   OS1Shell 2.0 by Logan Mitchell
*   Credits to websites referenced are listed as used in the code.
**/
int main(int argc, char** argv)
{
    printf("\n<<====OS1SHELL 2.0 POST====>>\n");
    if(argc <= 1)
    {
        printf("ERROR: No arguments passed into program. Please pass the name of a filesystem to load or create.");
        printf("\nPress enter to exit.");
        getchar();
        apocalypse();
    }

    string filename = string(argv[1]);
    setFileSystemPath(filename);

    //load a filesystem, or check for a new one before we continue.
    checkForFileSystem(string(argv[0]), string(argv[1]));

    printf("\n<<====OS1SHELL 2.0====>>\n(type 'help' for commands)\n\n");
    //main shell loop
	while(1)
	{
	    int readChars;
	    char* inputBuffer = new char[BUFFER_MAX_SIZE];

	    printf("os1shell ==> ", currentPath.c_str());
        fflush(stdout);

	    readChars = read(0,inputBuffer,BUFFER_MAX_SIZE);

        if(readChars == -1)
        {
            perror("\n**os1shell read error");
            if(errno == EIO)
            {
                //this will help in cases of opening other programs asynchronously which read from the console, such as bash.
                //after the initial error, os1shell will wait until the subprocess returns to begin reading again.
                //can't read from both at once anyway, so we force it to wait in these situations.
                wait(NULL);
            }
        }
	    else if(readChars >= BUFFER_MAX_SIZE && inputBuffer[BUFFER_MAX_SIZE-1] !='\n')
	    {
            //if the last char isnt \n but we're at max capacity we're over limit.
	        printf("Warning; input exceeded the allowed %d character limit.", (BUFFER_MAX_SIZE-1));

	        //the read buffer will continue with the overflow unless we clear it manually, so clear it before we continue.
            clearReadBufferOverflow();
	    }
	    else
	    {
            //if we have EOF condition from ctrl+D send kill command to program and all children.
            if(readChars == 0)
            {
                apocalypse();
            }
	        //if our last input was enter, we have a normal input to read
	        else if(inputBuffer[readChars-1] == '\n')
	        {
                parseAndExecute(inputBuffer, readChars);
            }

	    }
        printf("\n");
	}
	return 0;
}

/** Checks if a filesystem exists, and if not, calls a function to
*   have one made. If so, loads data from the boot record and uses it.
*
*   @param inputPath    Path to the folder containing the filesystem.
*   @param filename     Name of the fileSystem.
**/
void checkForFileSystem(string inputPath, string filename)
{
    //get paths to open files

    int removePos = inputPath.find("os1shell");
    string execPath = inputPath.erase(removePos);
    string openPath = (execPath+filename);

    //check if we can fopen the name of the file passed in
    fileSystem = fopen(openPath.c_str(), "r+");

    //if our pointer isnt null, the file system exists.
    if(fileSystem != NULL)
    {
        printf("\nFile system '%s' found.\n", filename.c_str());
        //load data from boot record
        unsigned int bootRecord[4];

        printf("Loading boot record...\n");

        getBootRecord(bootRecord);

        //store some data... 16 bytes of memory is cheap and I/O isnt.
        CLUSTER_SIZE_BYTES = bootRecord[0];
        FILE_SYSTEM_SIZE_BYTES = bootRecord[1];
        ROOT_DIR_INDEX = bootRecord[2];
        FAT_INDEX = bootRecord[3];

        printf("\nCluster Size: %u  File System Size: %u\nRoot Directory Index: %u  FAT Index: %u\n", CLUSTER_SIZE_BYTES, FILE_SYSTEM_SIZE_BYTES, ROOT_DIR_INDEX, FAT_INDEX);

        numClusters = ceil((double)(((double)FILE_SYSTEM_SIZE_BYTES)/CLUSTER_SIZE_BYTES));
        double minimumBytesNeeded  = (numClusters-2)*128;
        int clustersNeededForDirTable = ceil(minimumBytesNeeded/CLUSTER_SIZE_BYTES);
        MAX_FILES = numClusters-clustersNeededForDirTable-2;

        printf("\nFile system '%s' loaded.\n", filename.c_str());
    }
    else
    {
        //we need a filesystem, so ask the user if they want to make one.
        bool validAnswer = false;
        while(!validAnswer)
        {
            string input = "";
            printf("\nNo file system '%s' found. Create new file system? [Y] ", filename.c_str());
            getline(cin, input);

            if(input.compare("Y") == 0 || input.compare("") == 0)
            {
                validAnswer = createNewFileSystem(filename, openPath);
            }
            else if(input.compare("N") == 0)
            {
                printf("Cannot proceed without file system. Press enter to exit.");
                getchar();
                apocalypse();
            }
            else
            {
                printf("Invalid input. Please enter 'Y' to create new file system or 'N' to exit.");
            }

        }
    }

}

/** Creates a new filesystem after prompting the user for data.
*
*   @param filename     Name of the fileSystem.
*   @param openPath     Full directory path to the filesystem file.
*   @return     Whether the new file system was created successfully.
**/
bool createNewFileSystem(string filename, string openPath)
{
    int fileSystemSize = -1;
    int clusterSize = -1;

    fileSystemSize = requestFileSystemSize();
    printf("\nFile system set to %d MB.\n", fileSystemSize);

    clusterSize = requestClusterSize();
    printf("\nCluster size set to %d KB.\n", clusterSize);

    CLUSTER_SIZE_BYTES = clusterSize * 1024;
    FILE_SYSTEM_SIZE_BYTES = fileSystemSize * 1024*1024;
    numClusters = ceil((double)(((double)FILE_SYSTEM_SIZE_BYTES)/CLUSTER_SIZE_BYTES));
    int sizeNeededForFAT = numClusters*sizeof(int);

    //check that one cluster can hold all the entries in our FAT given the system size
    if(sizeNeededForFAT > CLUSTER_SIZE_BYTES)
    {
        printf("\nError: Cluster size not big enough to hold FAT volume. Reduce system size, or increase cluster size.");
        printf("\nDetails: %d byte FAT table is too large to contain in %d byte cluster.\n\n", sizeNeededForFAT, CLUSTER_SIZE_BYTES);
        return false; //send them back to initial prompt
    }

    //if we're good to this point, make the file system.
    fileSystem = fopen(openPath.c_str(), "w+");

    //set some information we know will be true about the new system
    FAT_INDEX = 1;
    ROOT_DIR_INDEX = 2;

    //fill the file system with all null clusters first to write it to disk
    printf("\n\nGenerating clusters...");
    for(int i=0; i < numClusters; i++)
    {
        //write a null cluster for each one.
        nullCluster(i);
    }

    setupBootRecord();

    setupFAT();

    //setup directory table. Determine how many clusters it will need to hold all the file info
    //then intialize.

    //need 128 bytes per potential file. (Clusters-2)*128 = bytes needed minimum. ceil(that/cluster_size_bytes) = clusters needed for dir_table
    double minimumBytesNeeded  = (numClusters-2)*128;
    int clustersNeededForDirTable = ceil(minimumBytesNeeded/CLUSTER_SIZE_BYTES);
    MAX_FILES = numClusters-clustersNeededForDirTable;
    fileInfo directory_table[numClusters];

    initializeDirTable(directory_table);

    //add entry for root
    strcpy(directory_table[0].filename, filename.c_str());
    directory_table[0].fileSize = clustersNeededForDirTable*CLUSTER_SIZE_BYTES;
    directory_table[0].index = ROOT_DIR_INDEX;
    directory_table[0].type = TYPE_DIRECTORY;
    directory_table[0].creationDate = time(NULL);

    //update FAT table for clusters being used by directory table
    for(int i=2; i < clustersNeededForDirTable; i++)
    {
        if(i < clustersNeededForDirTable-1)
            changeFATEntry(i, i+1);
        else
            changeFATEntry(i, 0xFFFF); //change last entry to indicate the end.
    }

    printf("\nWriting directory table...");
    writeDirectoryTable(directory_table);

    printf("\nFile system created. (Clusters: %d File Capacity: %d)\n\n", numClusters, MAX_FILES);

    return true;
}

/** Write a new file to the file system. Handles spanning multiple clusters.
*
*   @param filename     Name of the file.
*   @param data         The file data.
*   @param dataLength   Length of the data array.
*   @param isDirectory  Whether this file is a directory.
*   @return     Success of file write.
**/
int writeFile(char* filename, char* data, int dataLength, bool isDirectory)
{
    //check filename is less than 112 chars
    if(sizeof(*filename) >= 112)
    {
        printf("ERROR: Filename %s is too long. Limit is 111 characters. Unable to write file.", filename);
        return -1;
    }

    //we'll be keeping track of which clusters we fill as we go, so we need an expandable list.
    vector<int> clustersUsed;

    int success = writeData(data, dataLength, 0, &clustersUsed);

    if(success == 0)
    {
        //when writedata returns, change FAT and directory entries for clusters used
        //should be 'backwards' from last cluster to first based on writeData's implementation
        int previousCluster;
        for(int i=0; i < clustersUsed.size(); i++)
        {
            //for the last written-to cluster, we mark the end in the FAT table.
            //else mark the next cluster memory wise, which will be the previous cluster since we're reading backwards
            if(i==0)
                changeFATEntry(clustersUsed[i], 0xFFFF);
            else
                changeFATEntry(clustersUsed[i], previousCluster);

            previousCluster = clustersUsed[i];
        }

        //by the end of the loop, previousCluster will hold the first cluster's index, which we can use for our directory table entry.
        //add a new entry in the dir table for this file.
        if(!isDirectory)
            addDirectoryTableEntry(filename, previousCluster, dataLength*sizeof(char), TYPE_FILE, time(NULL));
        else
            addDirectoryTableEntry(filename, previousCluster, dataLength*sizeof(char), TYPE_DIRECTORY, time(NULL));

    }
    return success;
}

/** Reads a file from the file system. Handles spanning over multiple clusters.
*
*   @param filename         Name of the file to read.
*   @param fileContainer    Container to return the read data in.
*   @return     Success of file read.
**/
int readFile(char* filename, char* fileContainer)
{
    //get dir table
    fileInfo dirTable[numClusters];
    getDirectoryTable(dirTable);

    int index = -1;
    int size = -1;
    //check for filename, if exists get its size and starting cluster index
    for(int i=0; i < MAX_FILES; i++)
    {
        if(strcmp(dirTable[i].filename, filename)==0)
        {
            index = dirTable[i].index;
            size = dirTable[i].fileSize;
        }
    }

    if(index == -1)
    {
        printf("ERROR: No such file exists in this directory.");
        return -1;
    }

    //call readData with its starting index, size, file container
    int error = readData(index, size, fileContainer);

    return error;

}

/** Reads data from a chain of clusters, starting at clusterIndex.
*
*   @param  clusterIndex     The start point to begin reading data from.
*   @param  fileSize         Size of the file, in bytes.
*   @param  fileContainer    Container to return the read data in.
*   @return     Success of data read.
**/
int readData(int clusterIndex, int fileSize, char* fileContainer)
{
    //first get the FAT so we can lookup if there is a chain of clusters
    int FileAllocationTable[numClusters];
    getFAT(FileAllocationTable);

    //start at the starting index for the file
    int currentChainIndex = clusterIndex;
    int bytesRead = 0;
    int loopIndex = 0;


    while(true)
    {
        int error = fseek(fileSystem, currentChainIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
        //if we're at the end of the line, this is the last read. Only read how much data we have left for the file size.
        int pointerOffset = ((double)(CLUSTER_SIZE_BYTES*loopIndex))/sizeof(char);
        if(FileAllocationTable[currentChainIndex] == 0xFFFF)
        {
            fread(fileContainer+pointerOffset, fileSize-bytesRead, 1, fileSystem);
            return 0; //all done reading.
        }
        //otherwise...
        fread(fileContainer+pointerOffset, CLUSTER_SIZE_BYTES, 1, fileSystem);
        currentChainIndex = FileAllocationTable[currentChainIndex];
        loopIndex++;
        bytesRead += CLUSTER_SIZE_BYTES;
    }
}

/** Prints a file from disk to the screen in text form.
*
*   @param  fileName     Name of the file to print from the file system.
**/
void printFile(char* fileName)
{
    char* fileContainer;

    fileInfo dirTable[numClusters];
    getDirectoryTable(dirTable);

    int size = -1;
    //check for filename, if exists get its size and starting cluster index
    for(int i=0; i < MAX_FILES; i++)
    {
        if(strcmp(dirTable[i].filename, fileName)==0)
        {
            size = dirTable[i].fileSize;
        }
    }

    if(size == -1)
    {
        printf("ERROR: Could not find file '%s'", fileName);
        return;
    }

    fileContainer = (char*) malloc(size);

    readFile(fileName, fileContainer);

    printf("Contents of '%s':\n\n%s\n", fileName, string(fileContainer).c_str());

}

/** Writes data to the file system. Handles spanning multiple clusters.
*
*   @param  data            The data to write to the file system.
*   @param  bufferLength    Length of the data array.
*   @param  offset          Should enter at 0. Used for recursive calls to write to multiple clusters.
*   @param  clustersUsed    Vector to keep track of clusters used.
*   @return     Success of data write.
**/
int writeData(char* data, int bufferLength, int offset, vector<int>* clustersUsed)
{
    int success = 0;
    //calculate total size in bytes
    int totalSizeBytes = sizeof(char)*bufferLength - offset;

    //check for free cluster - if no clusters return -1
    int currentCluster = findNextFreeCluster(clustersUsed);

    if(currentCluster == -1)
    {
        //no clusters left!
        printf("\nERROR: Not enough space on disk for file.");
        return -1;
    }

    //add used cluster to list
    (*clustersUsed).push_back(currentCluster);

    bool needAnotherCluster = false;
    //write to free cluster. send in data+offset as data.
    if(totalSizeBytes <= CLUSTER_SIZE_BYTES)
    {
        writeCluster(currentCluster, data, totalSizeBytes, offset);
    }
    else
    {
        //data is bigger than cluster, we will need to write one clusters worth and do more after
        needAnotherCluster = true;
        writeCluster(currentCluster, data, CLUSTER_SIZE_BYTES, offset);
    }

    //if we need another, call writedata with new offset
    if(needAnotherCluster)
    {
        success = writeData(data, bufferLength, offset + CLUSTER_SIZE_BYTES, clustersUsed);
    }

    if(success == 0)
    {
        return 0;
    }
    else
    {
        //if not successful, null cluster we wrote to, return -1
        nullCluster(currentCluster);
        return -1;
    }
}

/** Finds the next free cluster, starting from cluster 0.
*
*   @param  clustersToIgnore    Vector list of clusters to ignore when searching.
*   @return     Index of the next free cluster, or -1 if no clusters avaliable.
**/
int findNextFreeCluster(vector<int>* clustersToIgnore)
{
    //find free cluster and return index
    int FileAllocationTable[numClusters];

    getFAT(FileAllocationTable);

    for(int i=0; i < numClusters; i++)
    {
        if(FileAllocationTable[i] == 0)
        {
            bool notOnList = true;
            for(int j=0; j < (*clustersToIgnore).size(); j++)
            {
                if(i == (*clustersToIgnore)[j])
                    notOnList = false;
            }

            if(notOnList)
                return i;

        }
    }

    return -1;

}

/** Changes an entry in the FAT table.
*
*   @param  index       Index of the FAT table entry to change.
*   @param  newValue    Value to change the table entry to.
**/
void changeFATEntry(int index, int newValue)
{
    //make sure not to change the boot record, or FAT table itself
    if(index == 0)
    {
        printf("Error: CANNOT CHANGE FAT INDEX 0. Belongs to: Boot Record");
        return;
    }
    if(index == FAT_INDEX)
    {
        printf("Error: CANNOT CHANGE FAT INDEX %d. Belongs to: File Allocation Table", index);
        return;
    }

    //read FAT into from disk
    int FileAllocationTable[numClusters];

    getFAT(FileAllocationTable);

    FileAllocationTable[index] = newValue;

    writeCluster(FAT_INDEX, FileAllocationTable, numClusters*sizeof(int), 0);
}

/** Adds an entry to the directory table.
*
*   @param  filename        Name of the file to add.
*   @param  startCluster    Start cluster of the new file.
*   @param  fileSizeBytes   File size of the new file, in bytes.
*   @param  fileType        Whether the new entry is a directory or file.
*   @param  createTime      Creation time of the new file, in Unix epoch time.
*   @return     Success of directory table entry addition.
**/
int addDirectoryTableEntry(char* filename, unsigned int startCluster, unsigned int fileSizeBytes, unsigned int fileType, unsigned int createTime)
{
    //grab the directory table
    fileInfo dirTable[numClusters];
    getDirectoryTable(dirTable);

    //find first unused entry
    int freeIndex = -1;
    for(int i=0; i< MAX_FILES; i++)
    {
        if(dirTable[i].filename[0] == 0x00 || dirTable[i].filename[0] == 0xFF)
        {
            freeIndex = i;
            break;
        }
    }

    if(freeIndex == -1)
    {
        printf("ERROR: No more room for directory table entries.");
        return -1;
    }

    //fill with info
    strcpy(dirTable[freeIndex].filename, filename);
    dirTable[freeIndex].fileSize = fileSizeBytes;
    dirTable[freeIndex].index = startCluster;
    dirTable[freeIndex].type = fileType;
    dirTable[freeIndex].creationDate = createTime;

    //write dir table out again
    writeDirectoryTable(dirTable);

    return 0;
}

/** Deletes a file from the current directory path.
*
*   @param  filename        Name of the file to delete.
*   @return     Success of file deletion.
**/
int deleteFile(string fileName)
{
    //grab the directory table
    fileInfo dirTable[numClusters];
    getDirectoryTable(dirTable);

    //find first unused entry
    int fileIndex = -1;
    for(int i=0; i< MAX_FILES; i++)
    {
        if(strcmp(dirTable[i].filename, fileName.c_str()) == 0)
        {
            fileIndex = i;
            break;
        }
    }

    if(fileIndex == -1)
    {
        printf("\nERROR: Couldn't find file in this directory.\n");
        return -1;
    }
    if(fileIndex == 0)
    {
        printf("\nERROR: Can't delete ROOT directory!\n");
        return -1;
    }

    //mark file as deleted.
    dirTable[fileIndex].filename[0] = 0xFF;

    //get the index so we can delete from FAT table
    int fIndex = dirTable[fileIndex].index;

    //write dir table out again
    writeDirectoryTable(dirTable);

   //first get the FAT so we can lookup the directory table chain of clusters
    int FileAllocationTable[numClusters];
    getFAT(FileAllocationTable);

    //start at the starting index for the directory table
    int currentChainIndex = fIndex;

    do
    {
        changeFATEntry(fIndex, 0x0000);
        currentChainIndex = FileAllocationTable[currentChainIndex];
    }
    while(currentChainIndex != 0xFFFF); //while we're not at the end of this file chain, keep reading parts of the file

    return 0;
}

/** Writes a single cluster to the file system.
*
*   @param  clusterIndex    Index of the cluster to write to.
*   @param  data            Data to write to the cluster.
*   @param  writeBytes      How many bytes to write to the cluster.
*   @param  offset          Offset into data to start from, in bytes.
**/
void writeCluster(int clusterIndex, int* data, int writeBytes, int offset)
{

    int offsetPointer = ((double)offset)/sizeof(int);

     //first empty the cluster
    nullCluster(clusterIndex);
    //seek to the cluster position we want
    int error = fseek(fileSystem, clusterIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
    if(error == 0)
    {
        fwrite(data+offsetPointer, 1, writeBytes, fileSystem);
    }
    else
    {
        printf("ERROR seeking within file system.");
    }
}

/** Writes a single cluster to the file system.
*
*   @param  clusterIndex    Index of the cluster to write to.
*   @param  data            Data to write to the cluster.
*   @param  writeBytes      How many bytes to write to the cluster.
*   @param  offset          Offset into data to start from, in bytes.
**/
void writeCluster(int clusterIndex, fileInfo* data, int writeBytes, int offset)
{

    int offsetPointer = ((double)offset)/sizeof(fileInfo);

     //first empty the cluster
    nullCluster(clusterIndex);
    //seek to the cluster position we want
    int error = fseek(fileSystem, clusterIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
    if(error == 0)
    {
        fwrite(data+offsetPointer, 1, writeBytes, fileSystem);
    }
    else
    {
        printf("ERROR seeking within file system.");
    }
}

/** Writes a single cluster to the file system.
*
*   @param  clusterIndex    Index of the cluster to write to.
*   @param  data            Data to write to the cluster.
*   @param  writeBytes      How many bytes to write to the cluster.
*   @param  offset          Offset into data to start from, in bytes.
**/
void writeCluster(int clusterIndex, char* data, int writeBytes, int offset)
{

    int offsetPointer = ((double)offset)/sizeof(char);

     //first empty the cluster
    nullCluster(clusterIndex);
    //seek to the cluster position we want
    int error = fseek(fileSystem, clusterIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
    if(error == 0)
    {
        fwrite(data+offsetPointer, 1, writeBytes, fileSystem);
    }
    else
    {
        printf("ERROR seeking within file system.");
    }
}

/** Writes a single cluster to the file system.
*
*   @param  clusterIndex    Index of the cluster to write to.
*   @param  data            Data to write to the cluster.
*   @param  writeBytes      How many bytes to write to the cluster.
*   @param  offset          Offset into data to start from, in bytes.
**/
void writeCluster(int clusterIndex, unsigned int* data, int writeBytes, int offset)
{

    int offsetPointer = ((double)offset)/sizeof(unsigned int);

     //first empty the cluster
    nullCluster(clusterIndex);
    //seek to the cluster position we want
    int error = fseek(fileSystem, clusterIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
    if(error == 0)
    {
        fwrite(data+offsetPointer, 1, writeBytes, fileSystem);
    }
    else
    {
        printf("ERROR seeking within file system.");
    }
}

/** Gets the boot record from the file system.
*
*   @param  bootRecordContainer     Return container to hold boot record.
**/
void getBootRecord(unsigned int* bootRecordContainer)
{
    //read 4 ints worth of data and thats it
    fseek(fileSystem, 0, SEEK_SET);
    fread(bootRecordContainer, 1, 4*sizeof(unsigned int),fileSystem);
}

/** Gets the FAT from the file system. Also makes you a little chubbier.
*
*   @param  FATContainer     Return container to hold FAT.
**/
void getFAT(int* FATContainer)
{
    int error = fseek(fileSystem, FAT_INDEX*CLUSTER_SIZE_BYTES, SEEK_SET);
    fread(FATContainer, 1, numClusters*sizeof(int),fileSystem);
}

/** Reads data from a file specified by the filePointer.
*
*   @param  FileContainer    Return container to hold file.
*   @param  fileSize         Size of the file to read.
*   @param  filePointer      Pointer to the file to read.
**/
void getFileData(char* FileContainer, int fileSize, FILE* filePointer)
{
    int error = fseek(filePointer, 0, SEEK_SET);
    fread(FileContainer, 1, fileSize,filePointer);
}

/** Writes the directory table to the file system.
*
*   @param  dirTable    Pointer to the directory table to write to the system.
**/
void writeDirectoryTable(fileInfo* dirTable)
{
    //call writeData
    int success = writeDirectoryTableClusters(dirTable, 0, ROOT_DIR_INDEX);

    if(success == -1)
    {
        printf("ERROR writing directory table! Written data undone.");
    }
}

/** Writes the directory table clusters to the file system.
*
*   @param  data                Data array to write to the file system.
*   @param  offset              Should enter at 0. Used for recursive calls.
*   @param  curDirTableIndex    Should enter at the first FAT index for the directory table.
**/
int writeDirectoryTableClusters(fileInfo* data, int offset, int curDirTableIndex)
{
    int FileAllocTable[numClusters];
    getFAT(FileAllocTable);

    int success = 0;

    writeCluster(curDirTableIndex, data, CLUSTER_SIZE_BYTES, offset);

    int nextIndex = FileAllocTable[curDirTableIndex];

    //if we're not at the end of the chain, get the next value from the fat
    if(nextIndex != 0xFFFF)
    {
        success = writeDirectoryTableClusters(data, offset + CLUSTER_SIZE_BYTES, nextIndex);
    }

    if(success == 0)
    {
        return 0;
    }
    else
    {
        //if upstream chain not successful, null cluster we wrote to, return -1
        nullCluster(curDirTableIndex);
        return -1;
    }
}

/** Gets the directory table from the file system.
*
*   @param  dirTableContainer     Return container to hold dir table.
**/
void getDirectoryTable(fileInfo* dirTableContainer)
{
    initializeDirTable(dirTableContainer);

    //first get the FAT so we can lookup the directory table chain of clusters
    int FileAllocationTable[numClusters];
    getFAT(FileAllocationTable);


    //start at the starting index for the directory table
    int currentChainIndex = ROOT_DIR_INDEX;

    int loopIndex = 0;
    do
    {
        //read the data from that cluster
        int error = fseek(fileSystem, currentChainIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
        //without the loop index multiplier to increment the pointer position
        //you keep adding to the start of the pointer with each read.
        //hours wasted finding this out: 3
        int pointerOffset = ((double)(CLUSTER_SIZE_BYTES*loopIndex))/sizeof(fileInfo);
        fread(dirTableContainer+pointerOffset, CLUSTER_SIZE_BYTES, 1, fileSystem);

        //find out whats next, if anything
        currentChainIndex = FileAllocationTable[currentChainIndex];
        loopIndex++;
    }
    while(currentChainIndex != 0xFFFF); //while we're not at the end of this file chain, keep reading parts of the file
}

/**
*   Prints the directory table. Duh.
**/
void printDirectoryTable()
{
    fileInfo directoryTable[numClusters];
    getDirectoryTable(directoryTable);

    int numFiles = 0;
    int numDirectory = 0;
    //printf("\n");
    //use max-files to ignore extra spaces that the fat/boot are using
    for(int i=0; i < MAX_FILES; i++)
    {
        if(directoryTable[i].filename[0] != 0x00 && directoryTable[i].filename[0] != 0xFFFFFFFF)
        {
            if(directoryTable[i].type == TYPE_FILE)
            {
                printf("\nFILE     '%s'  Size: %d   Created: %s   Cluster: %u", directoryTable[i].filename, directoryTable[i].fileSize, epochToDatetime(directoryTable[i].creationDate).c_str(), directoryTable[i].index);
                numFiles++;
            }
            else if(directoryTable[i].index == ROOT_DIR_INDEX)
            {
                printf("\nROOT-DIR '%s'  Size: %d   Created: %s   Cluster: %d", directoryTable[i].filename, directoryTable[i].fileSize, epochToDatetime(directoryTable[i].creationDate).c_str(), directoryTable[i].index);
                numDirectory++;
            }
            else if(directoryTable[i].type == TYPE_DIRECTORY)
            {
                printf("\nDIR      '%s'  Size: %d   Created: %s   Cluster: %d", directoryTable[i].filename, directoryTable[i].fileSize, epochToDatetime(directoryTable[i].creationDate).c_str(), directoryTable[i].index);
                numDirectory++;
            }
        }
    }
    printf("\n\n[OS1 FileSystem] Total %d Files %d Directories (Including ROOT)\n", numFiles, numDirectory);

}

/** Print the names of the files found in the file system.
*
*   @param  longDesc     Implements a -l type system that gives more information on the files in the file system.
**/
void printDirectoryNames(bool longDesc)
{
    fileInfo directoryTable[numClusters];
    getDirectoryTable(directoryTable);

    printf("\n");
    //use max-files to ignore extra spaces caused by using numClusters as our array size.
    for(int i=0; i < MAX_FILES; i++)
    {
        if(directoryTable[i].filename[0] != 0x00 && directoryTable[i].filename[0] != 0xFFFFFFFF)
        {
            if(!longDesc)
            {
                //short desc, names only
                    if(directoryTable[i].type == TYPE_FILE)
                    {
                        printf("  %s  ", directoryTable[i].filename);
                    }
                    else if(directoryTable[i].type == TYPE_DIRECTORY && i != 0)
                    {
                        printf("  [%s]  ", directoryTable[i].filename);
                    }
            }
            else
            {
                //longer desc, include more information
                if(directoryTable[i].type == TYPE_FILE)
                {
                    printf("\nFIL %*d %s %s", 12, directoryTable[i].fileSize, epochToDatetime(directoryTable[i].creationDate).c_str(), directoryTable[i].filename);
                }
                else if(directoryTable[i].type == TYPE_DIRECTORY && i != 0)
                {
                   printf("\nDIR %*d %s %s", 12, directoryTable[i].fileSize, epochToDatetime(directoryTable[i].creationDate).c_str(), directoryTable[i].filename);
                }
            }
        }
    }
    printf("\n");

}

/** Prints the FAT table from the file system.
*
*   @param  startCluster    Cluster to begin printing from.
*   @param  endCluster      Cluster to stop printing at.
**/
void printFAT(int startCluster, int endCluster)
{
    int FileAllocTable[numClusters];

    getFAT(FileAllocTable);

    printf("\nFAT Content [%d-%d]:", startCluster, endCluster);
    for(int i=startCluster; i<endCluster; i++)
    {
        if(i < numClusters)
        {
            if(i%10 == 0)
            {
                 printf("\n");
            }
            //printf("[%d:0x%X]", i, FileAllocTable[i]); //hex
            if(FileAllocTable[i] == 0xFFFF)
                printf("[%d:END/FULL]", i);
            else if(FileAllocTable[i] == 0)
                printf("[%d:FREE]", i);
            else if(FileAllocTable[i] == 0xFFFE)
                 printf("[%d:RSVD]", i);
            else
                printf("[%d:%d]", i, FileAllocTable[i]);
        }
    }
    printf("\n\n");
}

/** Fills a cluster with nulls.
*
*   @param  clusterIndex    Cluster to null.
**/
void nullCluster(int clusterIndex)
{
    char* writeBuffer = (char*)calloc(CLUSTER_SIZE_BYTES, '\0');
    int error = fseek(fileSystem, clusterIndex*CLUSTER_SIZE_BYTES, SEEK_SET);
    if(error == 0)
    {
        fwrite(writeBuffer, 1, CLUSTER_SIZE_BYTES, fileSystem);
    }
    else
    {
        printf("ERROR seeking within file system.");
    }

    free(writeBuffer);
}


/** Parses the buffer returns from read(), and sends an order
*   to execute the parameters created from the parse.
*
*   @param inputBuffer      The raw buffer that came from the previous read().
*   @param readChars        How many characters were read from the previous read().
*/
void parseAndExecute(char inputBuffer[], int readChars)
{
    //remove newline before parsing.
    inputBuffer[readChars-1] = '\0';

    //make a string for checking commands and adding to history easier.
    string command = inputBuffer;
    string originalCommand = string(command);

    //Exit gracefully.
    if(command.compare("exit") == 0)
    {
        exit(0);
    }
    else
    {
        //make the tokens we'll use for parsing.
        vector<string> tokens = tokenizeInput(inputBuffer, readChars-1);

        //check for blank input.
        if(tokens.size() > 0)
        {
            //check if last token has a '&' at the end so we know whether to wait on child.
            bool waitForChild = true;
            string lastToken = tokens[tokens.size()-1];
            if(lastToken.substr(lastToken.length()-1).compare("&") == 0)
            {
                //remove the & from the param/command
                tokens[tokens.size()-1] = lastToken.substr(0, lastToken.length()-1);

                //if the token was only a &, remove it.
                //this works for & trailing either with whitespace or not.
                //implentation is this way because that's how bash works.
                if(tokens[tokens.size()-1].length() == 0)
                {
                    tokens.pop_back();
                }

                waitForChild = false;
            }
            //continue if we have tokens at this point
            if(tokens.size() > 0)
            {
                //make a c-style string array out of tokens for use with execVp
                char* params [tokens.size()+1];

                makeParamsFromTokens(tokens, params);

                //if we're in our file system, dont fork/exec. Use our custom methods.
                //also maybe use ours for copy?
                if(checkForLocalHandling(params, tokens.size()+1))
                {
                    int numParams = tokens.size();

                    if(strcmp(params[0], "cd") == 0)
                    {
                        commandCd(params, numParams);
                    }
                    else if(strcmp(params[0], "ls") == 0)
                    {
                        commandLs(params, numParams);
                    }
                    else if(strcmp(params[0], "touch") == 0)
                    {
                        commandTouch(params, numParams);
                    }
                    else if(strcmp(params[0], "cp") == 0)
                    {
                        commandCp(params, numParams);
                    }
                    else if(strcmp(params[0], "mv") == 0)
                    {
                        commandMv(params, numParams);
                    }
                    else if(strcmp(params[0], "rm") == 0)
                    {
                        commandRm(params, numParams);
                    }
                    else if(strcmp(params[0], "df") == 0)
                    {
                        commandDf(params);
                    }
                    else if(strcmp(params[0], "cat") == 0)
                    {
                        commandCat(params, numParams);
                    }
                    else if(strcmp(params[0], "pdt") == 0)
                    {
                        printDirectoryTable();
                    }
                    else if(strcmp(params[0], "pfat") == 0)
                    {
                        printFAT(0, numClusters);
                    }
                    else if(strcmp(params[0], "help") == 0)
                    {
                        printf("\nList of commands:\n\ncd [Change Directory] Type no params for current directory\n");
                        printf("ls [List Files/Directories] -l functionality in project file system\n");
                        printf("touch [Create Empty File] Requires filename parameter\n");
                        printf("cp [Copy] Copy file. Requires source and destination parameters, with full paths\n");
                        printf("mv [Move] Move file. Requires source and destination parameters, with full paths\n");
                        printf("rm [Delete] Remove file. Requires target file as parameter\n");
                        printf("df [Describe Filesystem] Shows information about clusters and filesystem sizes\n");
                        printf("cat [File Contents] Shows text form of a file\n");
                        printf("pdt [Print Directory Table] Prints directory table for project file system\n");
                        printf("pfat [Print File Allocation Table] Prints full FAT for project file system\n");
                        printf("exit [Quit Shell] Exit OS1Shell\n");
                        printf("help [Show Commands] You made it here, didn't you?\n");
                    }
                    else
                    {
                        printf("\nERROR: UNKNOWN COMMAND.\n");
                    }
                }
                else
                {
                    if(strcmp(params[0], "touch") == 0)
                    {
                        if(tokens.size() == 2)
                        {
                            string newPath = currentPath;
                            if(!isLastCharSlash(currentPath))
                            {
                                 newPath += "/";
                            }

                            newPath += params[1];
                            tokens[1] = newPath;
                        }
                    }
                    else
                    {
                        //add path to the end of the params
                        if(strcmp(params[0], "cat") != 0)
                        {
                            tokens.push_back(currentPath);
                        }
                    }


                    params [tokens.size()+1];

                    makeParamsFromTokens(tokens, params);

                    forkAndExecute(params, waitForChild, originalCommand);
                }

            }
            else
            {
                printf("\nError: Invalid input.\n");
            }
        }
        else
        {
            //There were no tokens from the input.
            printf("\nError: Empty input.\n");
        }
    }
}

/**
*   Tokenizes the inputbuffer using strtok and returns
*   a vector of strings representing all the tokens.
*   Delimiters by spaces.
*
*   @param input    A c-style string to split into tokens.
*   @param length   Length of the input string.
*   @return         A vector array of tokens created by strtok.
**/
vector<string> tokenizeInput(char* input, int length)
{
    char inputVal[length];
    strcpy(inputVal, input);
    //cout << "Tokenizing input:" << inputVal << endl;
    vector<string> returnTokens;
    char* token;
    token = strtok(inputVal, " ");
    while(token != NULL)
    {
        string sToken = token;
        returnTokens.push_back(sToken);
        //cout << "Token " << sToken << " created." << endl;

        token = strtok(NULL, " ");
    }

    return returnTokens;
}


/** Makes parameters for execvp out of tokens.
*
*   @param tokens   Vector array of strings to make into parameters.
*   @param params   The c-style array of parameters to write to.
*/
void makeParamsFromTokens(vector<string> tokens, char* params[])
{
    for(int i=0; i < tokens.size(); i++)
    {
        params[i] = new char[tokens[i].size()+1];
        //tokens.c_str gives back an unusable read-only c_str, so use a custom method
        toCString(tokens[i], params[i]);
    }
    //make sure it's a null terminated array
    params[tokens.size()] = NULL;
}

/** Fork a child process to handle the execution of the parameters specfied.
*
*   @param params           The array of c-strings representing the arguments for execvp.
*   @param waitForChild     Whether to wait for the forked process to return before continuing.
*   @param originalCommand  The original, unaltered command to be entered into the history.
*/
void forkAndExecute(char* params[], bool waitForChild, string originalCommand)
{
    int execError;
    int forkId = fork();
    if(forkId == 0) //child process
    {
        printf("\n"); //format spacer

        execError = execvp(params[0], params);
        if(execError == -1)
        {
            perror("Error executing command");
            printf("('%s' is not a recognized command.)", params[0]);
            //exit child in this case with -1 to differentiate from an error where the program actually ran.
            exit(-1);
        }
        exit(0); //otherwise exit normally
    }
    else if(forkId > 0) //parent
    {
        if(waitForChild)
        {
            //advanced wait code idea from http://www.yolinux.com/TUTORIALS/ForkExecProcesses.html
            int childExitStatus;

            waitpid(forkId, &childExitStatus, 0);

            childExitStatus = WEXITSTATUS(childExitStatus);
        }

    }
    //handle fork error case using perror.
    else if(forkId == -1)
    {
        perror("Error forking");
    }
}


/** Sends a signal to the current process designated
*   by signalNum.
*
*   @param signalNum    The signal to trigger.
*/
void sendSelfSignal(int signalNum)
{
    pid_t my_pid = getpid();
    kill(my_pid, signalNum);
}

/** Clears the read buffer of overflow in the event
*   characters exceeding the local buffer limit are read.
*/
void clearReadBufferOverflow()
{
    bool overflow = true;
    while(overflow)
    {
         //find the null char at the end of the overflow by running through the read buffer one by one
         char* burnBuffer = new char[1];
         read(0,burnBuffer,1);
         if(burnBuffer[0] == '\n')
         {
             //once we've hit the new line char we're done
             overflow = false;
         }
    }
}

/** Converts a string into a null terminated c style string
*   that can be written to, unlke c_str.
*
*   @param inputString      The input string to convert.
*   @param outputCstring    The c-style string to write to.
*/
void toCString(string inputString, char outputCstring[])
{
    for(int i=0; i<inputString.length(); i++)
    {
        outputCstring[i] = inputString[i];
    }
    //terminate string with null char
    outputCstring[inputString.length()] = '\0';
}


/** Destorys the world.
*   Specifically, kills children processes,
*   frees memory, and then terminates self
*   using the kill command program.
*/
void apocalypse()
{
    //kill processes
    kill(0, 9);
}

/** Sets the fileSystemPath, which lets the system know what the home
*   path is for the file system.
*
*   @param  fileName    Name of the file system.
**/
void setFileSystemPath(string fileName)
{
    currentPath = fileSystemPath = string("/" + fileName);
}

/** Asks the user for a cluster size between 8 and 16 KB.
*
*   @return     The cluster size chosen.
**/
int requestClusterSize()
{
    //prompt for cluster size
     while(true)
    {
        string input = "";
        printf("\nEnter the cluster size for this system in KB [%d] ", DEFAULT_CLUSTER_SIZE);
        getline(cin, input);

        if(input.length() == 0)
        {
            //if they hit enter, just use default
            return DEFAULT_CLUSTER_SIZE;
        }
        else
        {
            int size = atoi(input.c_str());

            if(size >= 8 && size <= 16)
            {
                return size;
            }
            else
            {
                printf("\nInvalid input. Cluster size must be between 8 and 16 KB.\n");
            }
        }

    }
}

/** Asks the user for a file system size between 5 and 50 MB.
*
*   @return     The file system size chosen.
**/
int requestFileSystemSize()
{
    //prompt for file system size
    while(true)
    {
        string input = "";
        printf("\nEnter the maximum size for this system in MB [%d] ", DEFAULT_FILE_SYSTEM_SIZE);
        getline(cin, input);

        if(input.length() == 0)
        {
            //if they hit enter, just use default
            return DEFAULT_FILE_SYSTEM_SIZE;
        }
        else
        {
            int size = atoi(input.c_str());

            if(size >= 5 && size <= 50)
            {
                return size;
            }
            else
            {
                printf("\nInvalid input. File system must be between 5 and 50 MB.\n");
            }
        }

    }
}

/**
*   Sets up the boot record when creating a new file system.
**/
void setupBootRecord()
{
    //setup boot record
    unsigned int bootRecord[4];
    bootRecord[0] = CLUSTER_SIZE_BYTES;
    bootRecord[1] = FILE_SYSTEM_SIZE_BYTES;
    bootRecord[2] = ROOT_DIR_INDEX;
    bootRecord[3] = FAT_INDEX;

    printf("\nWriting following data to boot record:\n");
    printf("\nCluster Size: %u  File System Size: %u  Root Directory Index: %u  FAT Index: %u\n", bootRecord[0], bootRecord[1], bootRecord[2], bootRecord[3]);

    writeCluster(0, bootRecord, sizeof(unsigned int)*4, 0);
}

/**
*   Sets up the FAT when creating a new file system.
**/
void setupFAT()
{
     //setup FAT
    int FileAllocationTable[numClusters];

    //initialize FAT
    for(int i=0;i<numClusters;i++)
        FileAllocationTable[i] = 0x0000;

    //fill boot record (0), FAT location (1)
    FileAllocationTable[0] = 0xFFFF;
    FileAllocationTable[1] = 0xFFFF;

    printf("\nWriting FAT...");
    writeCluster(FAT_INDEX, FileAllocationTable, numClusters*sizeof(int), 0);
}

/**
*   Fills the directory table with blank values when creating a new file system.
**/
void initializeDirTable(fileInfo* directory_table)
{
    for(int i=0; i < numClusters; i++)
    {
        directory_table[i].filename[0] = 0x00;
        directory_table[i].filename[1] = '\0';
        directory_table[i].fileSize = 0;
        directory_table[i].index = i;
        directory_table[i].type = TYPE_FILE;
        directory_table[i].creationDate = time(NULL);
    }
}

/** Handles the 'cd' command for the file system.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandCd(char** params, int numParams)
{
    //insure proper number of params
    if(numParams == 1)
    {
        printf("\n%s\n", currentPath.c_str());
    }
    else if(numParams == 2)
    {
        char* changePath = params[1];

        if(strcmp(changePath,fileSystemPath.c_str()) == 0 || isDirectory(changePath))
        {
            currentPath = string(changePath);
        }
        else
        {
            printf("\nERROR: Not a directory, or invalid path.\n");
        }

    }
    else
    {
        printf("\nERROR: Invalid number of arguments for 'cd'\n");
    }
}

/** Handles the 'ls' command for the file system. Implements -l functionality.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandLs(char** params, int numParams)
{
    if(numParams == 1)
        printDirectoryNames(false);
    else if(numParams == 2)
    {
        if(strcmp(params[1], "-l") == 0)
        {
            printDirectoryNames(true);
        }
        else
        {
            printf("\nERROR: Unknown extension for 'ls'. Valid extensions are: '-l' when in project filesystem.\n");
        }
    }
    else
    {
        printf("\nERROR: Invalid number of arguments for 'ls'\n");
    }
}

/** Handles the 'touch' command for the file system.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandTouch(char** params, int numParams)
{
    if(numParams == 2)
    {
        if(!filenameExists(params[1]))
        {
            char data[0];
            int error = writeFile(params[1],data,0,false);
            if(error == 0)
            {
                printf("\nFile created.\n");
            }
        }
        else
        {
            printf("\nERROR: Unable to make file; Already a file with that name in this directory.\n");
        }
    }
    else
    {
        printf("\nERROR: Invalid number of arguments for 'touch'\n");
    }

}

/** Handles the 'cp' command for the file system.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandCp(char** params, int numparams)
{
    //check num params is correct.
    if(numparams != 3)
    {
        printf("\nERROR: Too few parameters for cp. Requires source and destination for copying.\n");
        return;
    }

    char* copyFromFilePath = params[1];
    char* copyToPath = params[2];
    int fileSize = 0;
    char filename[112];

    char* copyFromFileDataContainer;

    //first thing will always be a file. Check that it's not a directory.
    if(!isLocalPath(copyFromFilePath) && pathExists(copyFromFilePath) && !isDirectory(copyFromFilePath))
    {
        FILE* copyFromFilePointer = fopen(copyFromFilePath, "r");
        if(copyFromFilePointer == NULL)
        {
            printf("\nERROR: Error opening file to copy. Unable to copy file.\n");
            return;
        }

        //get the fileSize
        fileSize = getExternalFileSize(copyFromFilePath);

        toCString(getFileNameFromPath(copyFromFilePath), filename);

        //setup the container to hold the file data
        copyFromFileDataContainer = (char*) malloc(fileSize);

        //get the file data
        getFileData(copyFromFileDataContainer, fileSize, copyFromFilePointer);

        fclose(copyFromFilePointer);

    }
    else if(!isLocalPath(copyFromFilePath) && !pathExists(copyFromFilePath))
    {
        printf("\nERROR: Path to copy from does not exist.\n");
        return;
    }
    else if(!isLocalPath(copyFromFilePath) && isDirectory(copyFromFilePath))
    {
        printf("\nERROR: Path to copy from is a directory.\n");
        return;
    }
    else if(isLocalPath(copyFromFilePath))
    {
        if(isValidLocalPath(copyFromFilePath))
        {
            //if we're good, use readfile to get the file data.

            //get the name of the file
            toCString(getFileNameFromPath(copyFromFilePath), filename);

            //get file size
            fileSize = getLocalFileSize(filename);

            copyFromFileDataContainer = (char*) malloc(fileSize);

            int error = readFile(filename, copyFromFileDataContainer);
            if(error != 0)
            {
                printf("\nERROR: Error reading file from file system. Unable to copy.\n");
                return;
            }

        }
        else
        {
            printf("\nERROR: Path to copy from is invalid. Start project filepaths with the /[file system name]. Cannot use additional directories in project file system.\n");
            return;
        }
    }

    //COPY TO ********************************************************************************************+
    //check where we're copying to. do writeFile or fwrite from stored data

    //first check if its a local path.
    if(isLocalPath(copyToPath))
    {
        if(isValidLocalPath(copyToPath) && strlen(copyToPath) == fileSystemPath.length() || strlen(copyToPath) == (fileSystemPath.length()+1))
        {
            //if no name given, use the copy from name
            int error = writeFile(filename, copyFromFileDataContainer, fileSize, false);

            if(error != 0)
            {
                printf("\nERROR: Error writing file to file system. Unable to copy.\n");
                return;
            }
        }
        //if there was a new name given with the copy path
        else if(isValidLocalPath(copyToPath))
        {
            //get the name of the file
            char fileName2[112];
            toCString(getFileNameFromPath(copyToPath), fileName2);

            int error = writeFile(fileName2, copyFromFileDataContainer, fileSize, false);

            if(error != 0)
            {
                printf("\nERROR: Error writing file to file system. Unable to copy.\n");
                return;
            }
        }
        else
        {
            printf("\nERROR: Path to copy to is invalid. Start project filepaths with the /[file system name]. Cannot use additional directories in project file system.\n");
            return;
        }
    }
    //if not a local path, check if they specified the name of the file to write to
    else if(!isLocalPath(copyToPath))
    {
        //if its not a directory just use full path for fwrite
        FILE* writeStream = fopen(copyToPath, "w");

        fwrite(copyFromFileDataContainer, 1, fileSize, writeStream);

        fclose(writeStream);
    }
    else
    {
        printf("\nERROR: Path to copy to does not exist.\n");
        return;
    }
}

/** Handles the 'mv' command for the file system.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandMv(char** params, int numParams)
{
    //check num params is correct.
    if(numParams != 3)
    {
        printf("\nERROR: Too few parameters for mv. Requires source and destination for moving.\n");
        return;
    }

    char* copyFromFilePath = params[1];
    char* copyToPath = params[2];
    int fileSize = 0;
    char filename[112];

    char* copyFromFileDataContainer;
    bool fromIsLocal = false;

    //first thing will always be a file. Check that it's not a directory.
    if(!isLocalPath(copyFromFilePath) && pathExists(copyFromFilePath) && !isDirectory(copyFromFilePath))
    {
        FILE* copyFromFilePointer = fopen(copyFromFilePath, "r");
        if(copyFromFilePointer == NULL)
        {
            printf("\nERROR: Error opening file to copy. Unable to copy file.\n");
            return;
        }

        //get the fileSize
        fileSize = getExternalFileSize(copyFromFilePath);

        toCString(getFileNameFromPath(copyFromFilePath), filename);

        //setup the container to hold the file data
        copyFromFileDataContainer = new char[fileSize];

        //get the file data
        getFileData(copyFromFileDataContainer, fileSize, copyFromFilePointer);

        fclose(copyFromFilePointer);

    }
    else if(!isLocalPath(copyFromFilePath) && !pathExists(copyFromFilePath))
    {
        printf("\nERROR: Path to copy from does not exist.\n");
        return;
    }
    else if(!isLocalPath(copyFromFilePath) && isDirectory(copyFromFilePath))
    {
        printf("\nERROR: Path to copy from is a directory.\n");
        return;
    }
    else if(isLocalPath(copyFromFilePath))
    {
        if(isValidLocalPath(copyFromFilePath))
        {
            fromIsLocal = true;
            //if we're good, use readfile to get the file data.

            //get the name of the file
            toCString(getFileNameFromPath(copyFromFilePath), filename);

            //get file size
            fileSize = getLocalFileSize(filename);

            copyFromFileDataContainer = new char[fileSize];

            int error = readFile(filename, copyFromFileDataContainer);
            if(error != 0)
            {
                printf("\nERROR: Error reading file from file system. Unable to copy.\n");
                return;
            }

        }
        else
        {
            printf("\nERROR: Path to copy from is invalid. Start project filepaths with the /[file system name]. Cannot use additional directories in project file system.\n");
            return;
        }
    }

    //COPY TO ********************************************************************************************+
    //check where we're copying to. do writeFile or fwrite from stored data or pointer

    //first check if its a local path.
    if(isLocalPath(copyToPath))
    {
        if(isValidLocalPath(copyToPath) && strlen(copyToPath) == fileSystemPath.length() || strlen(copyToPath) == (fileSystemPath.length()+1))
        {
            //if no name given, use the copy from name
            int error = writeFile(filename, copyFromFileDataContainer, fileSize, false);

            if(error != 0)
            {
                printf("\nERROR: Error writing file to file system. Unable to copy.\n");
                return;
            }
        }
        //if there was a new name given with the copy path
        else if(isValidLocalPath(copyToPath))
        {
            //get the name of the file
            char fileName2[112];
            toCString(getFileNameFromPath(copyToPath), fileName2);

            int error = writeFile(fileName2, copyFromFileDataContainer, fileSize, false);

            if(error != 0)
            {
                printf("\nERROR: Error writing file to file system. Unable to copy.\n");
                return;
            }
        }
        else
        {
            printf("\nERROR: Path to copy to is invalid. Start project filepaths with the /[file system name]. Cannot use additional directories in project file system.\n");
            return;
        }
    }
    //if not a local path, check if they specified the name of the file to write to
    else if(!isLocalPath(copyToPath))
    {
        //if its not a directory just use full path for fwrite
        FILE* writeStream = fopen(copyToPath, "w");

        fwrite(copyFromFileDataContainer, 1, fileSize, writeStream);

        fclose(writeStream);
    }
    else
    {
        printf("\nERROR: Path to copy to does not exist.\n");
        return;
    }

    //once we're done moving the file, delete the old one.
    if(fromIsLocal)
    {
        deleteFile(filename);
    }
    else
    {
        unlink(copyFromFilePath);
    }
}

/** Handles the 'rm' command for the file system and home system.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandRm(char** params, int numParams)
{
    if(numParams == 2)
    {
        //check if our current path is in the project file system
        if(currentPath.compare(fileSystemPath) == 0)
        {
            int error = deleteFile(params[1]);
            if(error == 0)
            {
                printf("\nFile deleted.\n");
            }
        }
        else
        {
            //if we're in the home file system, we can't just add the path
            //to the end of the delete command and fork/exec, so we'll handle it ourselves.
            //adjust the path by using the stored current directory.
            string delPath = currentPath;
            if(!isLastCharSlash(currentPath))
            {
                 delPath += "/";
            }

            delPath += params[1];

            if(pathExists(delPath.c_str()))
            {
                if(!isDirectory(delPath))
                {
                    unlink(delPath.c_str());
                }
                else
                     printf("\nERROR: Cannot delete a directory.\n");
            }
            else
                 printf("\nERROR: File does not exist.\n");

        }
    }
    else
    {
        printf("\nERROR: Invalid number of arguments for 'rm'\n");
    }
}

/** Handles the 'df' command for the file system.
*
*   @param params      The parameters for the input.
*/
void commandDf(char** params)
{
    //get number of clusters used
    int clustersUsed = 0;

    int FileAllocTable[numClusters];

    getFAT(FileAllocTable);

    for(int i=0; i<numClusters; i++)
    {
        //if not free or reserved, tab it up
        if(FileAllocTable[i] == 0xFFFF)
            clustersUsed++;
        else if(FileAllocTable[i] == 0);
        else if(FileAllocTable[i] == 0xFFFE);
        else
           clustersUsed++;
    }

    int avaliableClusters = numClusters-clustersUsed;

    //show system info; name, numClusters, clusters used, clusters avaliable, some other stuff

    printf("\nFilesystem      Clusters    Used        Avaliable     Cluster Size   System Size");
    printf("\n--------------------------------------------------------------------------------");
    printf("\n%-*s %-*d %-*d %-*d   %-*d  %d\n", 15, fileSystemPath.c_str(), 11, numClusters, 11, clustersUsed, 11, avaliableClusters, 13, CLUSTER_SIZE_BYTES, FILE_SYSTEM_SIZE_BYTES);

}

/** Handles the 'cat' command for the file system.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*/
void commandCat(char** params, int numParams)
{
    if(numParams == 2)
    {
        if(filenameExists(params[1]))
            printFile(params[1]);
        else
            printf("\nERROR: Specified file does not exist.\n");
    }
    else
        printf("\nERROR: Invalid number of arguments for 'cat'\n");
}

/** Checks if we need to handle a command or if we can just fork/exec it.
*
*   @param params      The parameters for the input.
*   @param numParams   The number of parameters passed in.
*   @return     Whether we need to handle it with our custom command methods.
*/
bool checkForLocalHandling(char** params, int numParams)
{

    if(strcmp(params[0], "cp") == 0 || strcmp(params[0], "mv") == 0)
    {
        //if its one of these and our current path is not our home, we need to check
        //if one of the commands passed in reflects our home directory
        if(numParams == 3)
        {
            //if either of the parameters reflects our file system, then we have to handle it
            if(isLocalPath(params[1]) || isLocalPath(params[2]))
            {
                return true;
            }
        }
        else
        {
            //in case there are no or only one param; we'll deal.
            return true;
        }
    }
    else if(fileSystemPath.compare(currentPath) == 0 || strcmp(params[0], "cd") == 0)
    {
        //handle anything with customs when we're at our project filesystem
        return true;
    }

    //always handle rm since its not easy to just fork/exec with an extra path
    if(strcmp(params[0], "rm") == 0)
    {
        return true;
    }

    return false;

}

/** Checks if a file exists in the project filesystem.
*
*   @param name   Name of the file to look for.
*   @return     If the file exists.
*/
bool filenameExists(char* name)
{
    fileInfo directoryTable[numClusters];
    getDirectoryTable(directoryTable);

    //use max-files to ignore extra spaces that the fat/boot are using
    for(int i=0; i < MAX_FILES; i++)
    {
        if(strcmp(directoryTable[i].filename, name)==0)
        {
            return true;
        }
    }

    return false;
}

/** Converts from epoch to a datetime string using the standard time library.
*
*   @param epochTime    The epoch time in seconds to convert.
*   @return     A datetime string formatted %Y-%m-%d %H:%M:%S representing the epochTime passed in.
*/
string epochToDatetime(int epochTime)
{
    const time_t time = epochTime;

    char timeString[80];
    tm* timeStruct = localtime(&time);
    strftime(timeString, 80, "%Y-%m-%d %H:%M:%S", timeStruct);

    return string(timeString);

}

/** Determines if a path is a directory or not using stat()
*   help from http://stackoverflow.com/questions/4980815/c-determining-if-directory-not-a-file-exists-in-linux
*
*   @param filePath    The filepath to check.
*   @return     Whether the filepath is a directory or not.
*/
bool isDirectory(char* filePath)
{
    struct stat st;
    stat(filePath,&st);
    if(S_ISDIR(st.st_mode))
        return true;
    else
        return false;
}


/** Determines if a path is a directory or not using stat()
*   help from http://stackoverflow.com/questions/4980815/c-determining-if-directory-not-a-file-exists-in-linux
*
*   @param filePath    The filepath to check.
*   @return     Whether the filepath is a directory or not.
*/
bool isDirectory(string filePath)
{
    struct stat st;
    stat(filePath.c_str(),&st);
    if(S_ISDIR(st.st_mode))
        return true;
    else
        return false;
}

/** Gets the file size of a file outside the project file system.
*
*   @param filePath    The filepath of the file to check the size of.
*   @return     Filesize of the specified file.
*/
int getExternalFileSize(char* filePath)
{
    struct stat st;

    stat(filePath,&st);

    return st.st_size;
}

/** Gets the file size of a file inside the project file system.
*
*   @param filePath    The file name of the file to check the size of.
*   @return     Filesize of the specified file.
*/
int getLocalFileSize(char* filename)
{
    //grab the directory table
    fileInfo dirTable[numClusters];
    getDirectoryTable(dirTable);

    //find first unused entry
    int fileIndex = -1;
    for(int i=0; i< MAX_FILES; i++)
    {
        if(strcmp(dirTable[i].filename, filename) == 0)
        {
            return dirTable[i].fileSize;
        }
    }

    return -1;
}


/** Determines if a path is a valid filepath using stat()
*   help from http://stackoverflow.com/questions/4980815/c-determining-if-directory-not-a-file-exists-in-linux
*
*   @param filePath    The filepath to check.
*   @return     Whether the filepath is a valid filepath or not.
*/
bool pathExists(char* filePath)
{
    struct stat st;
    if(stat(filePath,&st) == 0)
        return true;
    else
        return false;
}

/** Determines if a path is a valid filepath using stat()
*   help from http://stackoverflow.com/questions/4980815/c-determining-if-directory-not-a-file-exists-in-linux
*
*   @param filePath    The filepath to check.
*   @return     Whether the filepath is a valid filepath or not.
*/
bool pathExists(string filePath)
{
    struct stat st;
    if(stat(filePath.c_str(),&st) == 0)
        return true;
    else
        return false;
}

/** Determines if a path is within the project file system
*
*   @param filePath    The filepath to check.
*   @return     Whether the filepath is in the project file system.
*/
bool isLocalPath(char* filePath)
{
    string path = string(filePath);

    string subPath = path.substr(0, fileSystemPath.length());

    if(subPath.compare(fileSystemPath) == 0)
        return true;
    else
        return false;
}

/** Determines if a path within the project file system has extraneous paths on it.
*
*   @param filePath    The filepath to check.
*   @return     Whether the path is valid or not.
*/
bool isValidLocalPath(char* localPath)
{
    string path = string(localPath);

    int pos = path.find(fileSystemPath, -1);

    //remove the fileSystemPath from the localPath, plus one for the slash between it and the filename
    string fileName = path.erase(0, fileSystemPath.length() +1);

    //check that there's no more slashes in the filename that would indicate further directories
    if(fileName.find("/") == -1)
    {
        return true;
    }

    //if we found another slash, this is invalid.
    //this will also prevent using a full path to the project filesystem.
    return false;
}

/** Gets a filename from a file path by clipping off whatever is after
*   the final / in the path.
*
*   @param filePath    The filepath to use to get the filename.
*   @return     The filename of the given filepath.
*/
string getFileNameFromPath(char* pathName)
{
    string filePath = string(pathName);

    int lastPos = filePath.find_last_of('/')+1;

    return filePath.substr(lastPos);
}

/** Checks if the final character in a filepath is a slash,
*   used for making valid paths since directories can have
*   slashes at the end or not.
*
*   @param filePath    The filepath to check
*   @return     If the final character in a filepath is a slash.
*/
bool isLastCharSlash(string filePath)
{
    string path = filePath;

    int pos = path.find_last_of('/');
    if(pos == path.length()-1)
        return true;

    return false;
}

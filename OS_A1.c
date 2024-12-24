#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <stdint.h>

typedef struct TrieNode{
    struct TrieNode * child[26];
    int count;
}TrieNode;

TrieNode * createNode(){
    TrieNode * newNode = (TrieNode *)malloc(sizeof(TrieNode));
    newNode->count = 0;
    for(int i = 0;i<26;i++){
        newNode->child[i] = NULL;
    }
    return newNode;
}

void insert(TrieNode * root,char * word){
    TrieNode * current = root;
    for(int i = 0;word[i]!='\0';i++){
        if(current->child[word[i]-'a']==NULL){
            current->child[word[i]-'a'] = createNode();
        }
        current = current->child[word[i]-'a'];
    }
    current->count++;
}

TrieNode * build(char * fileName){
    FILE * file = fopen(fileName,"r");
    TrieNode * root = createNode();
    char word[256];
    while(fscanf(file,"%s",word)!=EOF){
        insert(root,word);
    }
    fclose(file);
    return root;
}

int countOfWordOccurences(TrieNode * root,char * word){
    TrieNode * current = root;
    for(int i = 0;word[i]!='\0';i++){
        if(current->child[word[i]-'a']==NULL){
            return 0;
        }
        current = current->child[word[i]-'a'];
    }
    return current->count;
}

void freeTrie(TrieNode * root){
    for(int i = 0;i<26;i++){
        if(root->child[i]!=NULL){
            freeTrie(root->child[i]);
        }
    }
    free(root);
}
char * decode(char * word,key_t key){
    int len = strlen(word);
    char * newString = (char*)malloc(sizeof(char)*(len+1));
    for(int i = 0;word[i]!='\0';i++){
        newString[i] = (word[i]-'a'+key)%26 + 'a';
    }
    newString[len] = '\0';
    return newString;
}

int getKey(int sum,key_t keyHelper){
    struct{
        long msg_type;
        int data;
    }message;
    int msgId = msgget(keyHelper,0666 | IPC_CREAT);
    message.msg_type = 1;
    message.data = sum;
    if(msgsnd(msgId,&message,sizeof(message)-sizeof(message.msg_type),0)==-1){
        perror("Message sending failed");
        exit(1);
    }
    if(msgrcv(msgId,&message,sizeof(message)-sizeof(message.msg_type),2,0)==-1){
        perror("Message receiving failed");
        exit(1);
    }
    return message.data;
}

int main(int argc,char ** argv){
    char * number = argv[1];
    char wordFileName[256];
    char inputFileName[256];
    snprintf(wordFileName,sizeof(wordFileName),"words%s.txt",number);
    snprintf(inputFileName,sizeof(inputFileName),"input%s.txt",number);

    FILE * inputFile = fopen(inputFileName,"r");
    int n,maxLen,keyMatrix,keyHelper;
    fscanf(inputFile,"%d\n%d\n%d\n%d",&n,&maxLen,&keyMatrix,&keyHelper);
    fclose(inputFile);

    TrieNode * root = build(wordFileName);

    int shmId;
    char(*shmPtr)[n][maxLen];
    size_t size = n*n*maxLen*sizeof(char);
    key_t keyM = keyMatrix;
    key_t keyH = keyHelper;
    shmId = shmget(keyM,size,0666);
    shmPtr = (char(*)[n][maxLen])shmat(shmId, NULL, 0);

    int CaesarKey = 0;
    for(int i = 1;i<=2*n-1;i++){
        int startRow = 0;
        int startColumn = 0;
        if(i<=n){
            startRow = 0;
            startColumn = i-1;
        }
        else{
            startRow = i-n;
            startColumn = n-1;
        }
        int sum = 0;
        int row = startRow;
        int column = startColumn;
        while(row<n && column>=0){
            char * decodedWord = decode(shmPtr[row][column],CaesarKey);
            sum+=countOfWordOccurences(root,decodedWord);
            free(decodedWord);
            row++;
            column--;
        }
        CaesarKey = getKey(sum,keyH);
    }

    freeTrie(root);

    if(shmdt(shmPtr)==-1){
        perror("Shared memory detach failed");
        exit(1);
    }
    return 0;
}
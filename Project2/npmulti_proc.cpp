#include <sys/wait.h> //waitpid()
#include <sys/types.h> //pid_t
#include <stdio.h>      //getline()
#include <unistd.h>   //fork(),execv(),pipe
#include <iostream> //cin,cout,cerr
#include <fstream>
#include <string>     //string,substr()
#include <string.h>  //strdup
#include <vector>    //vector
#include <queue>
#include <map>
#include <set>       //set
#include <dirent.h>  //DIR,dirent,opendir
#include <fcntl.h>  //O_CREAT,O_RDWR,O_TRUNC,S_IREAD,S_IWRITE
#include <stdlib.h>
#include <sys/stat.h>//open(),S_IREAD,S_IWRITE
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>

using namespace std;

const int DONOTHING=0;
const int ISBROADCAST=1;
const int ISTELL=2;
const int STDIN=0;
const int STDOUT=1;
const int STDERR=2;
const int maxclient=30;
const int maxnamelen=20;
const int maxmsglen=1024;
const int BufSize=15000;
const string prompt = "% ";
const string welcome="****************************************\n** Welcome to the information server. **\n****************************************\n";
const string upipedir="user_pipe/";//"<sender>_<receiver>"
const key_t shmkey=5555;
const key_t semkey=6666;
int shmid,sem;
pid_t mypid;
set <string> Buildins;//目前PATH中有的指令
vector <int*> npipes;//number pipe的pipe
vector <int> npipedest;//number pipe的destination
map<int,int> fdmap;//key:id value:fd (for master to close unused fd) 

extern char **environ;//所有的環境變數

struct clientinfo{
    int fd;
    char name[maxnamelen];//user name
    int msgtype;//0:nothing 1:broadcast 2:tell 
    char msg[maxmsglen];//要傳的msg
    int tellid;//要tell的目標ID
};

struct shmMSG{
    struct clientinfo infos[maxclient+1];
};
struct shmMSG *shmmsg;
//wait for [2] (lock) to equal 0 ,then increment [2] to 1 - this locks it UNDO to release the lock if processes exits before explicitly unlocking */
static struct sembuf op_lock[2] ={2,0,0,2,1,SEM_UNDO};
//decrement [1] (proc counter) with undo on exit UNDO to adjust proc counter if process exits before explicitly calling sem_close() then decrement [2] (lock) back to 0 */
static struct sembuf op_endcreate[2] ={1,-1,SEM_UNDO,2,-1,SEM_UNDO};
//decrement [1] (proc counter) with undo on exit
static struct sembuf op_open[1] ={1,-1,SEM_UNDO};
//wait for [2] (lock) to equal 0 then increment [2] to 1 - this locks it then increment [1] (proc counter)
static struct sembuf op_close[3] ={2,0,0,2,1,SEM_UNDO,1,1,SEM_UNDO};
//decrement [2] (lock) back to 0
static struct sembuf op_unlock[1] = {2,-1,SEM_UNDO};
//decrement or increment [0] with undo on exit the 30 is set to the actual amount to add or subtract (positive or negative)
static struct sembuf op_op[1] = {0, 30,SEM_UNDO};
/*Create a semaphore with a specified initial value.
If the semaphore already exists, we don't initialize it (of course).
We return the semaphore ID if all OK, else -1.*/
int sem_create(key_t key, int initval){//used if we create the semaphore
    // register int id, semval;
    int id, semval;
    union semun {
        int val;
        struct semid_ds *buf;
        ushort *array;
    } semctl_arg;
    if(key == IPC_PRIVATE) {
        return -1; /* not intended  for private sem */
    }else if(key == (key_t) -1 ){
        return -1; /* provaly an ftok() error by caller */
    }
    again:
        if((id = semget(key, 3, 0666 | IPC_CREAT)) <0 ){
            return -1;/* permission problem or tables full */
        }
        /*When the semaphore is created, we know that the value of all 3 members is 0.
        Get a lock on the semaphore by waiting for [2] to equal 0, then increment it.
        There is a race condition here.  There is a possibility that between the semget() above and the semop() below, 
        another process can call our sem_close() function which can remove the semaphore if that process is the last one using it.
        Therefore, we handle the error condition of an invalid semaphore ID specially below, and if it does happen, 
        we just go back and create it again.*/
        if((semop(id, &op_lock[0], 2))<0){
            if(errno == EINVAL) goto again;
                cerr<<"can't lock"<<endl;
        }
        /*Get the value of the process counter.If it equals 0, then no one has initialized the semaphore yet.*/
        if((semval = semctl(id, 1, GETVAL, 0)) <0 ){
            cerr<<"can't GETVAL"<<endl;
        }
        if(semval == 0){ /* initial state */
        /*We could initialize by doing a SETALL, but that would clear the adjust value that we set when we locked the semaphore above.  
        Instead, we'll do 2 system calls to initialize [0] and [1].*/
            semctl_arg.val = initval;
            if(semctl(id, 0, SETVAL, semctl_arg) <0 ){
                cerr<<"can't SETVAL[0] "<<endl;
            }
            semctl_arg.val = maxclient+1;/* at most are 30 client to attach this file */
            if(semctl(id, 1, SETVAL, semctl_arg) <0 ){
                cerr<<"can't SETVAL[1] "<<endl;
            }
        }
        //Decrement the process counter and then release the lock.
        if(semop(id, &op_endcreate[0], 2) <0 ){
            cerr<<"can't end create "<<endl;
        }
    return id;
}
/*Remove a semaphore.
This call is intended to be called by a server, when it is being shut down, as we do an IPC_RMID on the semaphore,
regardless whether other processes may be using it or not.
Most other processes should use sem_close() below.*/
void sem_rm(int id){
    if(semctl(id, 0, IPC_RMID, 0) <0){
        cerr<<"can't IPC_RMID"<<endl;
    }
}
/*Open a semaphore that must already exist.
This function should be used, instead of sem_create(),
if the caller knows that the semaphore must already exist.
For example a client from a client-server pair would use this, 
if its the server's responsibility to create the semaphore.
We return the semaphore ID if all OK, else -1.*/
int sem_open(key_t key){
    // register int id;
    int id;
    if(key == IPC_PRIVATE) {//not intended for private semaphores
        return -1;
    }else if(key == (key_t) -1 ) {//probably an ftok() error by caller
        return -1;
    }
    if((id = semget(key, 3, 0)) <0 ) {
        return -1; /* doesn't exist or tables full*/
    }
    //Decrement the process counter.  We don't need a lock to do this.
    if(semop(id, &op_open[0], 1) <0 ){
        cerr<<"can't open "<<endl;
    }
    return id;
}
/*Close a semaphore.
This function is for a process to call before it exits, when it is done with the semaphore.
We "decrement" the counter of processes using the semaphore, and if this was the last one, we can remove the semaphore.*/
void sem_close(int id){
    // register int semval;
    int semval;
    if(semop(id, &op_close[0], 3) <0 ) {
        cerr<<"can't semop "<<endl;
    }
    if((semval = semctl(id, 1, GETVAL, 0)) <0) {
        cerr<<"can't GETVAL"<<endl;
    }
    if(semval > maxclient+1) {
        cerr<<"sem[1] > 31 "<<endl;
    }else if(semval == maxclient+1) {
        sem_rm(id);
    }else{
        if(semop(id, &op_unlock[0],1) <0) {
            cerr<<"can't unlock "<<endl;
        }
    }
}
//General semaphore operation.  Increment or decrement by a user-specified amount (positive or negative; amount can't be zero).
void sem_op(int id, int value){
    if((op_op[0].sem_op = value) == 0) {
        cerr<<"can't have value == 0 "<<endl;
    }
    if(semop(id, &op_op[0], 1) <0 ) {
        cerr<<"sem_op error "<<endl;
    }
}
/*Wait until a semaphore's value is greater than 0, then decrementit by 1 and return.*/
void sem_wait(int id,string func){
    sem_op(id, -1);
    // cout<<"Sem lock by "<<func<<endl;
}
//Increment a semaphore by 1.
void sem_signal(int id,string func){
    sem_op(id, 1);
    // cout<<"Sem unlock by "<<func<<endl;
}

string GetUpipeFileName(int sender,int receiver){
    return upipedir+to_string(sender)+"_"+to_string(receiver)+".txt";
}

void Broadcast(int id,string message){
    sem_wait(sem,"Broadcast");
    strcpy(shmmsg->infos[id].msg,message.c_str());
    shmmsg->infos[id].msgtype=ISBROADCAST;
    sem_signal(sem,"Broadcast");
    kill(mypid,SIGUSR1);
}

void Tell(int from,int to ,string message){
    string msg="";
    sem_wait(sem,"Tell");
    if(shmmsg->infos[to].fd<0){
        msg="*** Error: user #"+to_string(to)+" does not exist yet. ***\n";
        write(shmmsg->infos[from].fd,msg.c_str(),msg.length());
    }else{
        msg+="*** ";
        msg+=shmmsg->infos[from].name;
        msg+=(" told you ***: "+message+"\n");
        strcpy(shmmsg->infos[from].msg,msg.c_str());
        shmmsg->infos[from].tellid=to;
        shmmsg->infos[from].msgtype=ISTELL;
    }
    sem_signal(sem,"Tell");
    kill(mypid,SIGUSR1);
}

void Yell(int from,string message){
    sem_wait(sem,"Yell");
    string msg="*** ";
    msg+=shmmsg->infos[from].name;
    msg+=(" yelled ***: "+message+"\n");
    sem_signal(sem,"Yell");
    Broadcast(from,msg);
}

void Who(int me){
    string message="<ID>\t<nickname>\t<IP/port>\t<indicate me>\n";
    sem_wait(sem,"Who");
    for(int i=1;i<=maxclient;i++){
        if(shmmsg->infos[i].fd>0){
            message+=(to_string(i)+"\t"+strdup(shmmsg->infos[i].name)+"\tCGILAB/511");
            if(i==me){
                message+="\t<-me";
            }
            if(i!=maxclient){
                message+="\n";
            }
        }
    }
    write(shmmsg->infos[me].fd,message.c_str(),message.length());
    sem_signal(sem,"Who");
}

void Name(int id ,string name){
    bool samename=false;
    sem_wait(sem,"Name");
    for(int i=1;i<=maxclient;i++){
        if(!strcmp(shmmsg->infos[i].name,name.c_str())){
            samename=true;
            break;
        }
    }
    string message="";
    if(samename){
        message="*** User '"+name+"' already exists. ***\n";
        write(shmmsg->infos[id].fd,message.c_str(),message.length());
        sem_signal(sem,"Name (alredy exist)");
    }else{
        strcpy(shmmsg->infos[id].name,name.c_str());
        message="*** User from CGILAB/511 is named '"+name+"'. ***\n";
        sem_signal(sem,"Name (success)");
        Broadcast(id,message);
    }
}

void CloseStream(int target){
    if(close(target)<0){
        cerr<<"close error("<<target<<")-"<<strerror(errno)<<endl;
    }
}

void DupStream(int news,int olds){
    if(dup2(news,olds)<0){
        cerr<<"dup error("<<news<<" to "<<olds<<")-"<<strerror(errno)<<endl;
    }
}

vector<string> SplitString(const string line, const string delimeter){
    vector <string> v;
    size_t pos1, pos2;

    pos1 = 0;//從頭開始find
    pos2 = line.find(delimeter);//第一個delimeter的index
    while(string::npos != pos2){
        //將第一個substring push到vector
        v.push_back(line.substr(pos1, pos2-pos1));
        //從delimeter的下一個位址開始find
        pos1 = pos2 + delimeter.size();
        //下一個delimeter的index
        pos2 = line.find(delimeter, pos1);
    }
    //將最後一個substring push到vector
    if(pos1 != line.length()){
        v.push_back(line.substr(pos1));
    }
    return v;
}

char** VectorToChar(vector <string> v){
    //為了要在最後放NULL所以要多一格
    char** arg = new char* [v.size()+1];
    //將vector的資料轉為char*後放放入arg
    for (int i=0;i<v.size();i++){
        arg[i]=strdup(v[i].c_str());
    }
    arg[v.size()]=NULL;//最後一個放NULL
    return arg;//return char**
}

void CloseUnusedSocket(){
    sem_wait(sem,"CloseUnusedSocket");
    for(int i=1;i<=maxclient;i++){
        if(fdmap[i]>0){//原本有連線
            if(shmmsg->infos[i].fd<0){//已經關掉了
                CloseStream(fdmap[i]);//把它關掉
                fdmap[i]=-1;//更新fdmap
            }
        }
    }
    sem_signal(sem,"CloseUnusedSocket");
}

void CloseOtherSocket(int id){
    sem_wait(sem,"CloseOtherSocket");
    for(int i=1;i<=maxclient;i++){
        if(i!=id){//不是自己
            if(shmmsg->infos[i].fd>0){//user存在
                CloseStream(shmmsg->infos[i].fd);
            }
        }
    }
    sem_signal(sem,"CloseOtherSocket");
}

void ClearAllEnvironment(){
    int i = 1;
    char *s = *environ;
    vector<string> env;
    string temp;

    for (; s; i++) {
        temp=s;
        env=SplitString(temp,"=");
        if(unsetenv(env[0].c_str())<0){
            cerr<<strerror(errno)<<endl;
        }
        s=*(environ+i);
    }
}

void SEnv(string name ,string value){
    if(setenv(name.c_str(),value.c_str(),1)<0){
        cerr<<"SEnv : "<<strerror(errno)<<endl;
    }
}

void PEnv(int clientfd ,string name){
    char* value=getenv(name.c_str());
    if(value!=NULL){
        string res=string(value)+"\n";
        int i=write(clientfd,res.c_str() ,res.length());
    }
}

void SetBuildin(){
    char* path ;
    vector<string>paths;
    string temp;
    struct dirent* dp;
    DIR* dirp;

    Buildins.clear();
    path = getenv("PATH");
    if(path!=NULL){
        paths=SplitString(path,":");
        for(int i=0;i<paths.size();i++){
            dirp=opendir(paths[i].c_str());
            if(dirp){
                while((dp=readdir(dirp))!=NULL){
                    temp.assign(dp->d_name);
                    if(temp.find(".")==string::npos){
                        Buildins.insert(temp);}
                }
            }
            closedir(dirp);
        }
    }
}

void RemoveSpace ( vector<string> &v ) {
    for (int i=0;i<v.size();i++){
        while(v[i].front()==' '){//去除前面的space
            v[i]=v[i].substr(1);
        }
        while(v[i].back()==' '){//去除後面的space
            v[i]=v[i].substr(0,v[i].size()-1);
        }
    }
}

string SetInputFromUpipe(int sender,int receiver,string line){
    sem_wait(sem,"SetInputFromUpipe");
    if(shmmsg->infos[sender].fd<0){//user 不存在
        string msg="*** Error: user #"+to_string(sender)+" does not exist yet. ***\n";
        write(shmmsg->infos[receiver].fd,msg.c_str(),msg.length());
        sem_signal(sem,"SetInputFromUpipe (user not exist)");
        return "";
    }
    if(access(GetUpipeFileName(sender,receiver).c_str(),F_OK)==0){ //user pipe的存在
        string msg="*** ";
        msg+=shmmsg->infos[receiver].name;
        msg+=(" (#"+to_string(receiver)+") just received from ");
        msg+=shmmsg->infos[sender].name;
        msg+=(" (#"+to_string(sender)+") by '"+line+"' ***\n");
        sem_signal(sem,"SetInputFromUpipe (success)");
        Broadcast(receiver,msg);
        cout<<"=====Set input from "<<GetUpipeFileName(sender,receiver)<<"====="<<endl;
        while(1){
            sem_wait(sem,"Wait for broadcast");
            if(shmmsg->infos[receiver].msgtype==DONOTHING){
                sem_signal(sem,"Wait for broadcast");
            break;
            }
            sem_signal(sem,"Wait for broadcast");
        }
        return (GetUpipeFileName(sender,receiver));
    }
    string msg="*** Error: the pipe #"+to_string(sender)+"->#"+to_string(receiver)+" does not exist yet. ***\n";
    write(shmmsg->infos[receiver].fd,msg.c_str(),msg.length());
    sem_signal(sem,"SetInputFromUpipe (pipe not exist)");
    return "";
}

int SetInputFromNpipe(){
    for(int i=0;i<npipedest.size();i++){
        if(npipedest[i]==0){//如果有number pipe的指令距離歸零
            return i;
        }
    }
    return -1;
}

string SetOutputToUpipe(int sender,int receiver,string line){
    string msg="";
    sem_wait(sem,"SetOutputToUpipe");
    if(shmmsg->infos[receiver].fd<0){//user不存在
        msg="*** Error: user #"+to_string(receiver)+" does not exist yet. ***\n";
        write(shmmsg->infos[sender].fd,msg.c_str(),msg.length());
        sem_signal(sem,"SetOutputToUpipe (User not exist)");
        return "";
    }
    //目前的user pipe已經有東西
    if(access(GetUpipeFileName(sender,receiver).c_str(),F_OK)==0){
        msg="*** Error: the pipe #"+to_string(sender)+"->#"+to_string(receiver)+" already exists. ***\n";
        write(shmmsg->infos[sender].fd,msg.c_str(),msg.length());
        sem_signal(sem,"SetOutputToUpipe (already exist)");
        
        return "";
    }
    msg="*** ";
    msg+=shmmsg->infos[sender].name;
    msg+=(" (#"+to_string(sender)+") just piped '"+line+"' to ");
    msg+=shmmsg->infos[receiver].name;
    msg+=(" (#"+to_string(receiver)+") ***\n");
    sem_signal(sem,"SetOutputToUpipe (new upipe)");
    cout<<"=====Set out to "<<GetUpipeFileName(sender,receiver)<<"====="<<endl;
    Broadcast(sender,msg);
    while(1){
        sem_wait(sem,"Wait for broadcast");
        if(shmmsg->infos[sender].msgtype==DONOTHING){
            sem_signal(sem,"Wait for broadcast");
            break;
        }
        sem_signal(sem,"Wait for broadcast");
    }
    return (GetUpipeFileName(sender,receiver));
}

int SetOutputToNpipe(int id,int dest){
    for(int i=0;i<npipes.size();i++){
        if(npipedest[i]==dest){ //和此command目的距離相同
            return i;
        }
    }
    //如果沒有就建立一個新的number pipe
    int *p=new int[2];
    if((pipe(p))<0){
        cerr<<"SetOutputToNpipe error:"<<strerror(errno)<<endl;
    }
    npipes.push_back(p);//加入新的number pipe
    npipedest.push_back(dest);//設定此number pipe的目的指令距離
    return (npipes.size()-1);
}

void CloseNumberPipes(int source, int destination){
    for(int i=0;i<npipes.size();i++){
        if(i==source){
            close(npipes[i][1]);
        }else if(i==destination){
            close(npipes[i][0]);
        }else{
            close(npipes[i][0]);
            close(npipes[i][1]);
        }
    }
}

void ChildHandler(int signo){
    int status;
    bool isBroadcasting;
    while(waitpid(-1,&status,WNOHANG)>0){}
    while(1){
        isBroadcasting=false;
        sem_wait(sem,"ChildHandler");
        for(int i=1;i<=maxclient;i++){
            if(shmmsg->infos[i].msgtype!=DONOTHING){
                isBroadcasting=true;
            }
        }
        sem_signal(sem,"ChildHandler");
        if(!isBroadcasting){
            break;
        }
    }
    CloseUnusedSocket();
}

void MessageHandler(int s){
    sem_wait(sem,"MessageHandler");
    for(int i=1; i<=maxclient; i++){
        if(shmmsg->infos[i].msgtype==ISBROADCAST){
            for(int j=1;j<=maxclient; j++){
                if(shmmsg->infos[j].fd>0){
                    if(write(shmmsg->infos[j].fd, shmmsg->infos[i].msg, strlen(shmmsg->infos[i].msg))<0){
                        cerr<<strerror(errno)<<endl;
                    }
                }
            }
            strcpy(shmmsg->infos[i].msg, "");
            shmmsg->infos[i].msgtype = DONOTHING;
        }else if(shmmsg->infos[i].msgtype==ISTELL){
            if(write(shmmsg->infos[shmmsg->infos[i].tellid].fd, shmmsg->infos[i].msg, strlen(shmmsg->infos[i].msg))<0){
                cerr<<strerror(errno)<<endl;
            }
            strcpy(shmmsg->infos[i].msg, "");
            shmmsg->infos[i].msgtype = DONOTHING;
            shmmsg->infos[i].tellid=-1;
        }
    }
    sem_signal(sem,"MessageHandler");
}

void Launch(const char * file, char * const * argv){
    if(execvp(file,argv)==-1){
        if(!(Buildins.count(file))){
            cerr<<"Unknown command: ["<<file<< "]."<<endl;
        }else{
            cerr<<"Error(exec):"<<strerror(errno)<<endl;
        }
        exit(EXIT_FAILURE);
    }
}

void FixCommand(vector <string> &commands,string delimeter){
    //切出最後的command和寫入的檔名
    vector<string> temp=SplitString(commands[commands.size()-1],delimeter);
    //將原本的最後一個移除
    commands.pop_back();
    //改為正確的command
    commands.push_back(temp[0]);
    commands.push_back(temp[1]);
}

void ForkNewProcess(pid_t &pid){
    do{
        signal(SIGCHLD,ChildHandler);//殺殭屍
        pid=fork();//嘗試fork新的child process
        if(pid<0){//如果失敗則sleep
            usleep(1000);
        }
    }while(pid<0);
}

void DupUpipeToInput(string upath){
    if(upath==""){
        exit(EXIT_FAILURE);
    }else{
        int f=open(upath.c_str(),O_RDONLY, S_IREAD);
        cout<<"Dup input from "<<upath<<" where f = "<<f<<endl;
        CloseStream(STDIN);
        DupStream(f,STDIN);
        CloseStream(f);
    }
}

void DupUpipeToOutput(string upath){
    if(upath==""){
        exit(EXIT_FAILURE);
    }else{
        int f=open(upath.c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
        cout<<"Dup output to "<<upath<<" where f = "<<f<<endl;
        CloseStream(STDERR);
        DupStream(f,STDERR);
        CloseStream(STDOUT);
        DupStream(f,STDOUT);
        CloseStream(f);
    }
}

void DupNpipeToInput(int nsource){
    if(nsource>=0){
        CloseStream(STDIN);
        DupStream(npipes[nsource][0],STDIN);
    }
}

void CloseNumberPipeWhenLogOut(){
    for(int i=0;i<npipes.size();i++){
        CloseStream(npipes[i][0]);
        CloseStream(npipes[i][1]);
        delete[] npipes[i];
        npipes.erase(npipes.begin()+i);
        npipedest.erase(npipedest.begin()+i);
    }
}

void CloseUserPipeWhenLogOut(int id){
    string upath;
    for(int i=1;i<=maxclient;i++){
        upath=GetUpipeFileName(id,i);
        if(access(upath.c_str(),F_OK)==0){
            remove(upath.c_str());
        }
        upath=GetUpipeFileName(i,id);
        if(access(upath.c_str(),F_OK)==0){
            remove(upath.c_str());
        }
    }
}

void CloseAllUserPipe(){
    string upath;
    for(int i=1;i<=maxclient;i++){
        for(int j=1;j<=maxclient;j++){
            upath=GetUpipeFileName(i,j);
            if(access(upath.c_str(),F_OK)==0){
                remove(upath.c_str());
            }
            upath=GetUpipeFileName(j,i);
            if(access(upath.c_str(),F_OK)==0){
                remove(upath.c_str());
            }
        }
    }
}

void SharedMemoryHandler(int s){
    CloseAllUserPipe();
    if(shmdt(shmmsg) < 0){
        cerr<<"shmdt error"<<endl;
    }
    if(shmctl(shmid,IPC_RMID,(struct shmid_ds *) 0) < 0){
        cerr<<"cant remove shm"<<endl;
    }
    sem_rm(sem);
    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}

void ParseLine(int id,int clientfd,string line){
    vector<string> commands;
    queue<pid_t> pids;
    char** arg;
    pid_t pid;
    int nsource =-1, ndestination=-1;
    string usource="",udestination="";
    bool fromupipe=false ,toupipe=false;
    int sender , receiver;
    for(int i=0;i<npipedest.size();i++){
        npipedest[i]--;//將number pipe的指令目的距離-1
    }
    commands=SplitString(line," ");
    if(commands[0]=="setenv"){
        SEnv(commands[1],commands[2]);
    }else if(commands[0]=="printenv"){
        PEnv(clientfd,commands[1]);
    }else if(commands[0]=="who"){
        Who(id);
    }else if(commands[0]=="tell"){
        string msg="";
        for(int x=2;x<commands.size();x++){
            msg+=commands[x];
            if(x<commands.size()-1){
                msg+=" ";
            }
        }
        Tell(id,stoi(commands[1]),msg);
    }else if(commands[0]=="yell"){
        string msg="";
        for(int x=1;x<commands.size();x++){
            msg+=commands[x];
            if(x<commands.size()-1){
                msg+=" ";
            }
        }
        Yell(id,msg);
    }else if(commands[0]=="name"){
        Name(id,commands[1]);
    }else{
        if((line.find("|")!=string::npos)||(line.find("!")!=string::npos)){//需要pipe
            int pipenum,commandnum,f;
            bool tonpipe=false , tofile=false , toepipe=false ;
            commands=SplitString(line," | ");
            tofile=(commands[commands.size()-1].find(" > "))!=string::npos;
            fromupipe=commands[0].find("<")!=string::npos;
            toupipe=((commands[commands.size()-1].find(">"))!=string::npos)&&((commands[commands.size()-1].find(" > "))==string::npos);
            tonpipe=(commands[commands.size()-1].find("|"))!=string::npos;
            toepipe=(commands[commands.size()-1].find("!"))!=string::npos;
            commandnum=commands.size();
            pipenum=commandnum-1;
            int *pfd = new int [2*pipenum];
            if(tofile){//如果要寫到file
                FixCommand(commands," > ");
                f=open(commands[commands.size()-1].c_str(),O_CREAT|O_RDWR|O_TRUNC,S_IREAD|S_IWRITE);
                commands.pop_back();
            }
            if(tonpipe){//要寫到numberpipe
                FixCommand(commands,"|");
                ndestination=SetOutputToNpipe(id,stoi(commands[commands.size()-1]));
                commands.pop_back();
            }
            if(toepipe){//如果要寫到numberpipe且包含stderr
                FixCommand(commands,"!");
                ndestination=SetOutputToNpipe(id,stoi(commands[commands.size()-1]));
                commands.pop_back();
            }
            if(fromupipe){//如果要從user pipe讀資料
                vector<string> temp=SplitString(commands[0],"<");
                commands[0]=temp[0];
                sender=stoi(temp[1]);
                usource=SetInputFromUpipe(sender,id,line);
            }else{//沒有的話檢查有沒有要從number pipe讀資料
                nsource=SetInputFromNpipe();
            }
            if(toupipe){//要寫到userpipe
                FixCommand(commands,">");
                receiver=stoi(commands[commands.size()-1]);
                udestination=SetOutputToUpipe(id,receiver,line);
                commands.pop_back();
            }
            RemoveSpace(commands);
            for(int i=0;i<commandnum;i++){
                //取得第i個process要執行的command
                arg=VectorToChar(SplitString(commands[i]," "));
                //建立此process需要的pipe
                if(pipenum>0){
                    if(i<pipenum){
                        if((pipe(pfd+2*i))<0){
                            cerr<<"Make pipe error:"<<strerror(errno)<<endl;
                        }
                    }
                    if(i>1){
                        CloseStream(pfd[2*(i-2)]);
                        CloseStream(pfd[2*(i-2)+1]);
                    }
                }
                ForkNewProcess(pid);
                if(fromupipe){
                    pids.push(pid);
                }else if(!tonpipe && !toepipe){
                    pids.push(pid);
                }
                if(pid==0){
                    if(!(i==0)){//除了第一個process都從pipe取出input
                        CloseStream(STDIN);
                        DupStream(pfd[2*(i-1)],STDIN);
                        CloseStream(pfd[2*(i-1)+1]);
                    }else{//第一個process
                        if(fromupipe){//從user pipe取資料
                            DupUpipeToInput(usource);
                        }else{//從number pipe取資料
                            DupNpipeToInput(nsource);
                        }
                    }
                    if(!(i==pipenum)){//除了最後一個process之外都將output寫到pipe
                        CloseStream(STDERR);
                        DupStream(clientfd,STDERR);
                        CloseStream(STDOUT);
                        DupStream(pfd[2*i+1],STDOUT);
                        CloseStream(pfd[2*i]);
                    }else{//最後一個process
                        if(tofile){//如果最後要寫到file
                            CloseStream(STDOUT);
                            DupStream(f,STDOUT);
                            CloseStream(f);
                        }else if(tonpipe){//如果最後要寫到number pipe
                            CloseStream(STDERR);
                            DupStream(clientfd,STDERR);
                            CloseStream(STDOUT);
                            DupStream(npipes[ndestination][1],STDOUT);
                        }else if(toepipe){//如果最後要寫到number pipe含error
                            CloseStream(STDERR);
                            DupStream(npipes[ndestination][1],STDERR);
                            CloseStream(STDOUT);
                            DupStream(npipes[ndestination][1],STDOUT);
                        }else if(toupipe){//如果最後要寫到user pipe
                            DupUpipeToOutput(udestination);
                        }else{
                            CloseStream(STDERR);
                            DupStream(clientfd,STDERR);
                            CloseStream(STDOUT);
                            DupStream(clientfd,STDOUT);
                        }
                    }
                    CloseNumberPipes(nsource,ndestination);
                    Launch(arg[0],arg);
                }
            }
            if(pipenum>0){//parent process將最後一個pipe關掉
                CloseStream(pfd[2*pipenum-2]);
                CloseStream(pfd[2*pipenum-1]);
            }
            delete[] pfd;
        }else if(line.find(" > ")!=string::npos){//輸出到檔案
            int f , sender;
            commands=SplitString(line," > ");
            for(int i=0;i<commands.size();i++){
                if(commands[i].find("<")!=string::npos){
                    fromupipe=true;
                    vector <string> v=SplitString(commands[i],"<");
                    commands[i]=v[0];
                    sender=stoi(v[1]);
                    usource=SetInputFromUpipe(sender,id,line);
                    break;
                }
                if(i==commands.size()-1){
                nsource=SetInputFromNpipe();
                }
            }
            RemoveSpace(commands);
            arg=VectorToChar(SplitString(commands[0]," "));
            ForkNewProcess(pid);
            pids.push(pid);
            if(pid==0){
                if(fromupipe){
                    DupUpipeToInput(usource);
                }else{
                    DupNpipeToInput(nsource);
                }
                CloseNumberPipes(nsource,-1);
                f=open(commands[1].c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
                DupStream(f,STDOUT);
                CloseStream(f);
                Launch(arg[0],arg);
            }
        }else if(line.find(">")!=string::npos){//輸出到user pipe
            int sender,receiver;
            commands=SplitString(line,">");
            for(int i=0;i<commands.size();i++){
                if(commands[i].find("<")!=string::npos){
                    fromupipe=true;
                    vector <string> v=SplitString(commands[i],"<");
                    commands[i]=v[0];
                    sender=stoi(v[1]);
                    usource=SetInputFromUpipe(sender,id,line);
                    break;
                    }
                if(i==commands.size()-1){
                    nsource=SetInputFromNpipe();
                }
            }
            RemoveSpace(commands);
            receiver=stoi(commands[1]);
            udestination=SetOutputToUpipe(id,receiver,line);
            arg=VectorToChar(SplitString(commands[0]," "));
            ForkNewProcess(pid);
            pids.push(pid);
            if(pid==0){
                if(fromupipe){
                    DupUpipeToInput(usource);
                }else{
                    DupNpipeToInput(nsource);
                }
                DupUpipeToOutput(udestination);
                CloseNumberPipes(nsource,-1);
                Launch(arg[0],arg);
            }
        }else{//只有單一command
            int sender;
            string cmd=line;
            if(line.find("<")!=string::npos){
                fromupipe=true;
                vector <string> v=SplitString(line," <");
                cmd=v[0];
                sender=stoi(v[1]);
                usource=SetInputFromUpipe(sender,id,line);
            }else{
                nsource=SetInputFromNpipe();
            }
            commands=SplitString(cmd," ");
            arg=VectorToChar(commands);
            ForkNewProcess(pid);
            pids.push(pid);
            if(pid==0){
                if(fromupipe){
                    DupUpipeToInput(usource);
                }else{
                    DupNpipeToInput(nsource);
                }
                CloseNumberPipes(nsource,-1);
                CloseStream(STDERR);
                DupStream(clientfd,STDERR);
                CloseStream(STDOUT);
                DupStream(clientfd,STDOUT);
                Launch(arg[0],arg);
            }
        }
    }
    //parent將用完的number pipe關掉
    for(int i=0;i<npipes.size();i++){
        if(npipedest[i]==0){
            CloseStream(npipes[i][0]);
            CloseStream(npipes[i][1]);
            delete[] npipes[i];
            npipes.erase(npipes.begin()+i);
            npipedest.erase(npipedest.begin()+i);
        }
    }
    while(!(pids.empty())){
        int status;
        waitpid(pids.front(),&status,0);
        pids.pop();
    }
    //parent將用完的user pipe關掉
    if(access(usource.c_str(),F_OK)==0){
        remove(usource.c_str());
    }
}

int main(int argc, char *argv[]){
    if(argc<2){
        cerr<<"Usage: [port]"<<endl;
        exit(EXIT_FAILURE);
    }
    CloseAllUserPipe();
    int clientid=0;
    pid_t pid;
    int serverfd; //listener
    int clientfd; 
    struct sockaddr_in server,client;
    char buf [BufSize];
    socklen_t addrlen = sizeof(client);
    int yes = 1;
    string line="";
    mypid=getpid();
    signal(SIGINT, SharedMemoryHandler);
    signal(SIGUSR1, MessageHandler);
    if ((shmid=shmget(shmkey,sizeof(struct shmMSG),IPC_CREAT | 0666))<0) {
        cerr<<"shmget error : "<<strerror(errno)<<endl;
        exit(EXIT_FAILURE);
    }
    shmmsg = (struct shmMSG*) shmat(shmid, (char *)0, 0);
    if ((sem = sem_create(semkey, 1)) < 0){
        cerr<<"sem_create error"<<endl;
    }
    if ((sem = sem_open(semkey)) < 0){
        cerr<<"sem open error"<<endl;
    }
    sem_wait(sem,"initial");
    for(int i=1;i<=maxclient;i++){
        shmmsg->infos[i].fd=-1;//設定fd的初始值
        fdmap[i]=-1;
        strcpy(shmmsg->infos[i].name,"");//設定name的初始值
        shmmsg->infos[i].msgtype=DONOTHING;
    }
    sem_signal(sem,"initial");
    memset (&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[1]));
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    serverfd=socket(AF_INET,SOCK_STREAM,0);
    if(serverfd<0){
        cerr<<"Create socket error: "<<strerror(errno)<<endl;
        exit(EXIT_FAILURE);
    } 
    cout<<"listener : "<<serverfd<<endl;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    cout<<"start binding"<<endl;
    bind(serverfd,(struct sockaddr *)&server,sizeof(server));
    // if(bind(serverfd,(struct sockaddr *)&server,sizeof(server)) < 0){
    //     cerr<<"Bind error: "<<strerror(errno)<<endl;
    //     exit(EXIT_FAILURE);
    // }
    cout<<"start listening"<<endl;
    if(listen(serverfd,100)<0){
        cerr<<"listen error: "<<strerror(errno)<<endl;
        exit(EXIT_FAILURE);
    }
    while(1){
        clientfd = accept(serverfd,(struct sockaddr*) &client, &addrlen);
        cout<<"accept:"<<clientfd<<endl;
        if(clientfd == -1){
            cerr<<"Accept error: "<<strerror(errno)<<endl;
        }else{//設定new client ID
            sem_wait(sem,"Setting new client");
            for(int j=1;j<=maxclient;j++){
                if(shmmsg->infos[j].fd==-1){
                    cout<<"-----New user : ID="<<j<<"-----"<<endl;
                    clientid=j;
                    shmmsg->infos[j].fd=clientfd;
                    fdmap[j]=clientfd;
                    strcpy(shmmsg->infos[j].name,"(no name)");
                    write(shmmsg->infos[j].fd,welcome.c_str(),welcome.length());
                    string msg="*** User '";
                    msg+=shmmsg->infos[j].name;
                    msg+="' entered from CGILAB/511. ***\n";
                    sem_signal(sem,"Setting new client (log in msg)");
                    Broadcast(clientid,msg);
                    sem_wait(sem,"Setting new client (write prompt)");
                    write(shmmsg->infos[j].fd,prompt.c_str(),prompt.length());
                    break;
                }
            }
            sem_signal(sem,"Setting new client");
        }
        ForkNewProcess(pid);
        if(pid==0){
            CloseOtherSocket(clientid);
            ClearAllEnvironment();//移除所有環境變數
            SEnv("PATH","bin:.");//設定PATH
            while(1){
                SetBuildin();//將目前PATH中的指令加入buildin set
                memset(&buf, 0, BufSize);
                line="";
                do{//讀取完整指令
                    read(clientfd,buf,BufSize);
                    line+=buf;
                }while(line.find("\n")==string::npos);
                if(line.find("\n")!=string::npos){
                    line=line.substr(0,line.length()-1);
                }
                if(line.find("\r")!=string::npos){
                    line=line.substr(0,line.length()-1);
                }
                cout<<"receive : ["<<line<<"] from user "<<clientid<<endl;
                if(line=="exit"){
                    sem_wait(sem,"Log out msg");
                    string msg="*** User '";
                    msg+=shmmsg->infos[clientid].name;
                    msg+="' left. ***";
                    sem_signal(sem,"Log out msg");
                    Broadcast(clientid,msg);//廣播此user的登出訊息
                    CloseStream(shmmsg->infos[clientid].fd);//關掉連線
                    CloseNumberPipeWhenLogOut();//清除此user的number pipe
                    CloseUserPipeWhenLogOut(clientid);//清除此user的user pipe
                    while(1){//防止%印在Broadcast前面
                        sem_wait(sem,"Wait for broadcast");
                        if(shmmsg->infos[clientid].msgtype==DONOTHING){
                            sem_signal(sem,"Wait for broadcast");
                            break;
                        }
                        sem_signal(sem,"Wait for broadcast");
                    }
                    sem_wait(sem,"Remove User");
                    shmmsg->infos[clientid].fd=-1;//將此id設為可用
                    shmmsg->infos[clientid].msgtype=DONOTHING;
                    strcpy(shmmsg->infos[clientid].name,"");//移除此user的Nickname
                    sem_signal(sem,"Remove User");
                    cout<<"-----User "<<clientid<<" logout-----"<<endl;
                    exit(EXIT_SUCCESS);
                }
                if(line!=""){
                    ParseLine(clientid,clientfd,line);
                    while(1){//防止%印在Broadcast前面
                        sem_wait(sem,"Wait for broadcast");
                        if(shmmsg->infos[clientid].msgtype==DONOTHING){
                            sem_signal(sem,"Wait for broadcast");
                            break;
                        }
                        sem_signal(sem,"Wait for broadcast");
                    }
                    write(clientfd,prompt.c_str(),prompt.length());
                }
            }
        }
    }
}
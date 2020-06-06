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

using namespace std;

const int STDIN=0;
const int STDOUT=1;
const int STDERR=2;
const int maxclient=30;
const int BufSize=15000;
const string prompt = "% ";
const string welcome="****************************************\n** Welcome to the information server. **\n****************************************\n";

fd_set master; //master file descripter list
map<int,int> fdmap;//key:id value:fd
map<int,string>clientname;//nickname
map<int , map<string,string> > clientenv;//key:id value:<key:name value :value>
set <string> Buildins;//目前PATH中有的指令
vector <int*> upipes;//user pipe的pipe
vector <int> upipesource;//user pipe的sender
vector <int> upipedest;//user pipe的receiver
vector <int*> npipes;//number pipe的pipe
vector <int> npipeowner;//number pipe的持有者
vector <int> npipedest;//number pipe的destination

extern char **environ;//所有的環境變數

void Broadcast(string message){
    for(int i = 1; i <= maxclient; i++) {
        if(fdmap[i]>0){
            if (FD_ISSET(fdmap[i], &master)) {
                if (write(fdmap[i],message.c_str(), message.length())<0) {
                    cerr<<"Write error : "<<strerror(errno)<<endl;
                }
            }
        }
    }
}

void Tell(int from,int to ,string message){
    string msg;
    if(fdmap[to]<0){
        msg="*** Error: user #"+to_string(to)+" does not exist yet. ***\n";
        write(fdmap[from],msg.c_str(),msg.length());
    }else{
        string msg="*** "+clientname[from]+" told you ***: "+message+"\n";
        write(fdmap[to],msg.c_str(),msg.length());
    }
}

void Yell(int from,string message){
    string msg="*** "+clientname[from]+" yelled ***: "+message+"\n";
    Broadcast(msg);
}

void Who(int me){
    string message="<ID>\t<nickname>\t<IP/port>\t<indicate me>\n";
    for(int i=1;i<=maxclient;i++){
        if(fdmap[i]>0){
            message+=(to_string(i)+"\t"+clientname[i]+"\tCGILAB/511");
            if(i==me){
                message+="\t<-me";
            }
            if(i!=maxclient){
                message+="\n";
            }
        }
    }
    write(fdmap[me],message.c_str(),message.length());
}

void Name(int id ,string name){
    bool samename=false;
    for(int i=1;i<=maxclient;i++){
        if(clientname[i]==name){
            samename=true;
            break;
        }
    }
    string message="";
    if(samename){
        message="*** User '"+name+"' already exists. ***\n";
        write(fdmap[id],message.c_str(),message.length());
    }else{
        clientname[id]=name;
        message="*** User from CGILAB/511 is named '"+name+"'. ***\n";
        Broadcast(message);
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

void SetAllEnvironment(int id){
    map<string,string> env=clientenv[id];
    for (map<string,string>::iterator it=env.begin(); it!=env.end(); ++it){
        if(setenv((it->first).c_str(),(it->second).c_str(),1)<0){
            cerr<<"SetAllEnvironment error : "<<strerror(errno)<<endl;
        }
    }
}

void SEnv(int id,string name ,string value){
    if(setenv(name.c_str(),value.c_str(),1)<0){
        cerr<<"SEnv : "<<strerror(errno)<<endl;
    }
    clientenv[id][name]=value;
}

void PEnv(int id ,string name){
    char* value=getenv(name.c_str());
    if(value!=NULL){
        string res=string(value)+"\n";
        write(fdmap[id],res.c_str() ,res.length());
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

int SetInputFromUpipe(int sender,int receiver,string line){
    if(fdmap[sender]<0){//user 不存在
        string msg="*** Error: user #"+to_string(sender)+" does not exist yet. ***\n";
        write(fdmap[receiver],msg.c_str(),msg.length());
        return -1;
    }
    for(int i=0;i<upipesource.size();i++){
        //如果有user pipe的存在，就把此index return
        if((upipesource[i]==sender)&&(upipedest[i]==receiver)){
            upipesource[i]=0;
            string msg="*** "+clientname[receiver]+" (#"+to_string(receiver)+") just received from "+clientname[sender]+" (#"+to_string(sender)+") by '"+line+"' ***\n";
            Broadcast(msg);
            return i;
        }
    }
    string msg="*** Error: the pipe #"+to_string(sender)+"->#"+to_string(receiver)+" does not exist yet. ***\n";
    write(fdmap[receiver],msg.c_str(),msg.length());
    return -1;
}

int SetInputFromNpipe(){
    for(int i=0;i<npipedest.size();i++){
        if(npipedest[i]==0){//如果有number pipe的指令距離歸零
            return i;
        }
    }
    return -1;
}

int SetOutputToUpipe(int sender ,int receiver,string line){
    if(fdmap[receiver]<0){//user不存在
        string msg="*** Error: user #"+to_string(receiver)+" does not exist yet. ***\n";
        write(fdmap[sender],msg.c_str(),msg.length());
        return -1;
    }
    //目前的user pipe已經有東西
    for(int i=0;i<upipesource.size();i++){
        if((upipesource[i]==sender)&&(upipedest[i]==receiver)){
            string msg="*** Error: the pipe #"+to_string(sender)+"->#"+to_string(receiver)+" already exists. ***\n";
            write(fdmap[sender],msg.c_str(),msg.length());
            return -1;
        }
    }
    //如果沒有就建立一個新的user pipe
    int *p=new int[2];
    if((pipe(p))<0){
        cerr<<"SetOutputTouPipe error:"<<strerror(errno)<<endl;
    }
    upipes.push_back(p);//加入新的user pipe
    upipesource.push_back(sender);//設定此user pipe的sender
    upipedest.push_back(receiver);//設定此user pipe的receiver
    string msg="*** "+clientname[sender]+" (#"+to_string(sender)+") just piped '"+line+"' to "+clientname[receiver]+" (#"+to_string(receiver)+") ***\n";
    Broadcast(msg);
    return (upipesource.size()-1);
}

int SetOutputToNpipe(int id,int dest){
    for(int i=0;i<npipes.size();i++){
        if(npipeowner[i]==id){ //是此user持有
            if(npipedest[i]==dest){ //和此command目的距離相同
                return i;
            }
        }
    }
    //如果沒有就建立一個新的number pipe
    int *p=new int[2];
    if((pipe(p))<0){
        cerr<<"SetOutputToNpipe error:"<<strerror(errno)<<endl;
    }
    npipes.push_back(p);//加入新的number pipe
    npipedest.push_back(dest);//設定此number pipe的目的指令距離
    npipeowner.push_back(id);//設定此number pipe的持有者
    return (npipes.size()-1);
}

void CloseUserPipes(int source, int destination){
    for(int i=0;i<upipes.size();i++){
        if(i==source){
            close(upipes[i][1]);
        }else if(i==destination){
            close(upipes[i][0]);
        }else{
            close(upipes[i][0]);
            close(upipes[i][1]);
        }
    }
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
    while(waitpid(-1,&status,WNOHANG)>0){}
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

void DupUpipeToInput(int usource){
    if(usource>=0){
        CloseStream(STDIN);
        DupStream(upipes[usource][0],STDIN);
    }else{
        exit(EXIT_FAILURE);
    }

}

void DupUpipeToOutput(int udestination){
    if(udestination>=0){
        cout<<"111111"<<endl;
        CloseStream(STDERR);
        cout<<"222222"<<endl;
        DupStream(upipes[udestination][1],STDERR);
        cout<<"333333"<<endl;
        CloseStream(STDOUT);
        cout<<"444444"<<endl;
        DupStream(upipes[udestination][1],STDOUT);
        cout<<"555555"<<endl;
    }else{
        cout<<"666666"<<endl;
        exit(EXIT_FAILURE);
    }
}

void DupNpipeToInput(int nsource){
    if(nsource>=0){
        CloseStream(STDIN);
        DupStream(npipes[nsource][0],STDIN);
    }
}

void CloseNumberPipeWhenLogOut(int id){
    for(int i=0;i<npipes.size();i++){
        if(npipeowner[i]==id){
            CloseStream(npipes[i][0]);
            CloseStream(npipes[i][1]);
            delete[] npipes[i];
            npipes.erase(npipes.begin()+i);
            npipedest.erase(npipedest.begin()+i);
            npipeowner.erase(npipeowner.begin()+i);
        }
    }
}

void CloseUserPipeWhenLogOut(int id){
    for(int i=0;i<upipes.size();i++){
        if(upipedest[i]==id){
            CloseStream(upipes[i][0]);
            CloseStream(upipes[i][1]);
            delete[] upipes[i];
            upipes.erase(upipes.begin()+i);
            upipedest.erase(upipedest.begin()+i);
            upipesource.erase(upipesource.begin()+i);
        }
    }
}

void ParseLine(int id,string line){
    vector<string> commands;
    queue<pid_t> pids;
    char** arg;
    pid_t pid;
    int usource =-1, udestination=-1, nsource =-1, ndestination=-1;
    bool fromupipe=false ,toupipe=false;
    for(int i=0;i<npipedest.size();i++){
        if(npipeowner[i]==id){//如果是這個id的number pipe
            npipedest[i]--;//將number pipe的指令目的距離-1
        }
    }
    commands=SplitString(line," ");
    if(commands[0]=="setenv"){
        SEnv(id,commands[1],commands[2]);
    }else if(commands[0]=="printenv"){
        PEnv(id,commands[1]);
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
            int pipenum,commandnum,f,sender,receiver;
            bool tonpipe=false , tofile=false , toepipe=false ;
            cout<<"here"<<endl;
            cout<<"there"<<endl;
            commands=SplitString(line," | ");
            tofile=(commands[commands.size()-1].find(" > "))!=string::npos;
            fromupipe=commands[0].find("<")!=string::npos;
            toupipe=((commands[commands.size()-1].find(">"))!=string::npos)&&((commands[commands.size()-1].find(" > "))==string::npos);
            tonpipe=(commands[commands.size()-1].find("|"))!=string::npos;
            toepipe=(commands[commands.size()-1].find("!"))!=string::npos;
            commandnum=commands.size();
            pipenum=commandnum-1;
            int *pfd= new int [2*pipenum];
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
                cout<<"executing : "<<commands[i]<<endl;
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
                if(!tonpipe && !toepipe){
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
                        DupStream(fdmap[id],STDERR);
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
                            DupStream(fdmap[id],STDERR);
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
                            DupStream(fdmap[id],STDERR);
                            CloseStream(STDOUT);
                            DupStream(fdmap[id],STDOUT);
                        }
                    }
                    CloseNumberPipes(nsource,ndestination);
                    CloseUserPipes(usource,udestination);
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
                CloseUserPipes(usource,-1);
                f=open(commands[1].c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
                DupStream(f,STDOUT);
                CloseStream(f);
                Launch(arg[0],arg);
            }
        }else if(line.find(">")!=string::npos){//輸出到user pipe
            int sender=-1,receiver=-1;
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
            if(pid==0){
                if(fromupipe){
                    DupUpipeToInput(usource);
                }else{
                    DupNpipeToInput(nsource);
                }
                DupUpipeToOutput(udestination);
                CloseNumberPipes(nsource,-1);
                CloseUserPipes(usource,udestination);
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
                CloseUserPipes(usource,-1);
                CloseStream(STDERR);
                DupStream(fdmap[id],STDERR);
                CloseStream(STDOUT);
                DupStream(fdmap[id],STDOUT);
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
            npipeowner.erase(npipeowner.begin()+i);
        }
    }
    //parent將用完的user pipe關掉
    for(int i=0;i<upipes.size();i++){
        if(upipesource[i]==0){
            CloseStream(upipes[i][0]);
            CloseStream(upipes[i][1]);
            delete[] upipes[i];
            upipes.erase(upipes.begin()+i);
            upipesource.erase(upipesource.begin()+i);
            upipedest.erase(upipedest.begin()+i);
        }
    }
    while(!(pids.empty())){
        int status;
        waitpid(pids.front(),&status,0);
        pids.pop();
    }
    

}

int main(int argc, char *argv[]){
    if(argc<2){
        cerr<<"Usage: [port]"<<endl;
        exit(EXIT_FAILURE);
    }
    int serverfd; //listener
    int clientfd; 
    struct sockaddr_in server,client;
    char buf [BufSize];
    socklen_t addrlen = sizeof(client);
    fd_set readfds;//file descripter list for select
    int fdmax=0;
    int yes = 1;
    string line="";

    FD_ZERO(&master); // 清除 master
    FD_ZERO(&readfds);// 清除 temp
    memset (&server, 0, sizeof (server)) ;
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[1]));
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    serverfd=socket(AF_INET,SOCK_STREAM,0);
    if (serverfd<0){
        cerr<<"Create socket error: "<<strerror(errno)<<endl;
        exit(EXIT_FAILURE);
    }
    cout<<"listener : "<<serverfd<<endl;
    for(int i=1;i<=maxclient;i++){
        fdmap[i]=-1;//設定fdmap的初始值
    }
    fdmap[0]=serverfd;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    cout<<"start binding"<<endl;
    if(bind(serverfd,(struct sockaddr *)&server,sizeof(server))<0){
        cerr<<"Bind error: "<<strerror(errno)<<endl;
        exit(EXIT_FAILURE);
    }
    cout<<"start listening"<<endl;
    if(listen(serverfd,100)<0){
        cerr<<"listen error: "<<strerror(errno)<<endl;
        exit(EXIT_FAILURE);
    }
    FD_SET(serverfd, &master); // 將 listener 新增到 master set
    fdmax = serverfd;// 持續追蹤最大的 file descriptor
    while(1){
        readfds = master; //複製master
        cout<<"start selecting"<<endl;
        cout<<"npipe : "<<npipes.size()<<" "<<npipeowner.size()<<" "<<npipedest.size()<<endl;
        cout<<"upipe : "<<upipes.size()<<" "<<upipesource.size()<<" "<<upipedest.size()<<endl;
        if(select(fdmax+1, &readfds, NULL, NULL, NULL)<0){
            cerr<<"Select error: "<<strerror(errno)<<endl;
        }else{
            for(int i = 0; i <=maxclient ; i++) {
                if(fdmap[i]>0){
                    if (FD_ISSET(fdmap[i],&readfds)){//找到一個
                    cout<<"from id-"<<i<<endl;
                        if (i == 0){//新連線
                            clientfd = accept(serverfd,(struct sockaddr*) &client, &addrlen);
                            cout<<"accept:"<<clientfd<<endl;
                            if(clientfd == -1){
                                cerr<<"Accept error: "<<strerror(errno)<<endl;
                            }else{
                                FD_SET(clientfd, &master);//新增到 master set
                                if (clientfd > fdmax) {// 持續追蹤最大的 fd
                                    fdmax = clientfd;
                                }
                                for(int j=1;j<=maxclient;j++){
                                    if(fdmap[j]==-1){
                                        fdmap[j]=clientfd;
                                        clientname[j]="(no name)";
                                        clientenv[j]["PATH"]="bin:.";
                                        write(fdmap[j],welcome.c_str(),welcome.length());
                                        string msg="*** User '"+clientname[j]+"' entered from CGILAB/511. ***\n";
                                        Broadcast(msg);
                                        write(fdmap[j],prompt.c_str(),prompt.length());
                                        break;
                                    }
                                }
                            }
                        }else{//來自client
                            ClearAllEnvironment();//移除所有環境變數
                            SetAllEnvironment(i);//設定目前client的環境變數
                            SetBuildin();//將目前PATH中的指令加入buildin set
                            memset(&buf, 0, BufSize);
                            line="";
                            do{//讀取完整指令
                                read(fdmap[i],buf,BufSize);
                                line+=buf;
                            }while(line.find("\n")==string::npos);
                            if(line.find("\n")!=string::npos){
                                line=line.substr(0,line.length()-1);
                            }
                            if(line.find("\r")!=string::npos){
                                line=line.substr(0,line.length()-1);
                            }
                            cerr<<"receive : ["<<line<<"]"<<endl;
                            if(line=="exit"){
                                string msg="*** User '"+clientname[i]+"' left. ***";
                                Broadcast(msg);//廣播此user的登出訊息
                                FD_CLR(fdmap[i], &master); // 從 master set 中移除
                                CloseStream(fdmap[i]);//關掉連線
                                fdmap[i]=-1;//將第i個ID設為可用
                                clientname[i]="";//移除此user的Nickname
                                // clientinfos[i]="";//移除此user的IP/Port
                                clientenv.erase(clientenv.find(i));//移除此user的環境變數
                                CloseNumberPipeWhenLogOut(i);//清除此user的number pipe
                                CloseUserPipeWhenLogOut(i);//清除此user的user pipe
                            }
                            if(line!=""){
                                ParseLine(i,line);
                                write(fdmap[i],prompt.c_str(),prompt.length());
                            }
                        }
                    }
                }
            }
        }
    }
}
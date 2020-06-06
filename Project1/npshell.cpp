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
#include <set>       //set
#include <dirent.h>  //DIR,dirent,opendir
#include <fcntl.h>  //O_CREAT,O_RDWR,O_TRUNC,S_IREAD,S_IWRITE
#include <stdlib.h>
#include <sys/stat.h>//open(),S_IREAD,S_IWRITE
#include <errno.h>
#include <signal.h>

using namespace std;

const int STDIN=0;
const int STDOUT=1;
const int STDERR=2;

set <string>  Buildins;
vector <int*> npipes;
vector <int>  pipedests;

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
    //從頭開始find
    pos1 = 0;
    //第一個空格的位址
    pos2 = line.find(delimeter);
    //find失敗會returnstring::npos
    while(string::npos != pos2){
        //將第一個substring push到vector
        v.push_back(line.substr(pos1, pos2-pos1));
        //從空格的下一個位址開始find
        pos1 = pos2 + delimeter.size();
        //下一個空格的位址
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
    //最後一個放NULL
    arg[v.size()]=NULL;
    return arg;
}

void SEnv(string name ,string value){
    setenv(name.c_str(),value.c_str(),1);
}

void PEnv(string name){
    char* value=getenv(name.c_str());
    if(value!=NULL){
        cout<<value<<endl;
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

void WriteToLog(string line){
    ofstream logfile;
    logfile.open("log.txt",ios::app);
    logfile<<line<<"\n";
    logfile.close();
}

string ReadLine(void){
    string line = "";
    getline(cin,line);
    WriteToLog(line);
    //讀到EOF則結束程式
    if (cin.eof()){
        line="exit";
    }
    return line;
}

void RemoveSpace ( vector<string> &v ) {
    for (int i=0;i<v.size();i++){
        //去除前面的space
        while(v[i].front()==' '){
            v[i]=v[i].substr(1);
        }
        //去除後面的space
        while(v[i].back()==' '){
            v[i]=v[i].substr(0,v[i].size()-1);
        }
    }
}

int SetInput(){
    //如果有number pipe的指令距離歸零，就把此pipe導入stdin
    for(int i=0;i<pipedests.size();i++){
        if(pipedests[i]==0){
            return i;
        }
    }
    return -1;
}

int SetOutputToPipe(int dest){
    //如果目前的number pipe有和此process目標指令相同的，就導入相同的numberpipe
    for(int i=0;i<npipes.size();i++){
        if(pipedests[i]==dest){
            return i;
        }
    }
    //如果沒有就建立一個新的number pipe
    int *p=new int[2];
    if((pipe(p))<0){
        cerr<<"SetOutputToPipe(make new number pipe) error:"<<strerror(errno)<<endl;
    }
    //加入新的number pipe
    npipes.push_back(p);
    //設定此number pipe的目的指令距離
    pipedests.push_back(dest);
    return (npipes.size()-1);
}

//將沒用的number pipe關掉
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
        //殺殭屍
        signal(SIGCHLD,ChildHandler);
        //嘗試fork新的child process
        pid=fork();
        //如果失敗則sleep
        if(pid<0){
            usleep(1000);
        }
    }while(pid<0);
}

void ParseLine(string line){
    vector<string> commands;
    queue<pid_t> pids;
    char** arg;
    pid_t pid;
    int source =-1;
    int destination=-1;
    //將numberpipe的指令目的距離-1
    for(int i=0;i<npipes.size();i++){
        pipedests[i]--;
    }
    // 需要pipe
    if((line.find("|")!=string::npos)||(line.find("!")!=string::npos)){
        int pipenum,commandnum,f;
        bool topipe=false , tofile=false , toepipe=false;
        commands=SplitString(line," | ");
        tofile=(commands[commands.size()-1].find(">"))!=string::npos;
        topipe=(commands[commands.size()-1].find("|"))!=string::npos;
        toepipe=(commands[commands.size()-1].find("!"))!=string::npos;
        commandnum=commands.size();
        pipenum=commandnum-1;
        int *pfd = new int [2*pipenum];
        source=SetInput();
        //如果要寫到file
        if(tofile){
            FixCommand(commands," > ");
            //打開要寫入的檔
            f=open(commands[commands.size()-1].c_str(),O_CREAT|O_RDWR|O_TRUNC,S_IREAD|S_IWRITE);
            commands.pop_back();
        }
        //如果要寫到pipe(numberpipe)
        if(topipe){
            FixCommand(commands,"|");
            destination=SetOutputToPipe(stoi(commands[commands.size()-1]));
            commands.pop_back();
        }
        //如果要寫到pipe(numberpipe)且包含stderr
        if(toepipe){
            FixCommand(commands,"!");
            destination=SetOutputToPipe(stoi(commands[commands.size()-1]));
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
            if(!topipe && !toepipe){
                pids.push(pid);
            }
            if(pid==0){
                //除了第一個process都從pipe取出input
                if(!(i==0)){
                    CloseStream(STDIN);
                    DupStream(pfd[2*(i-1)],STDIN);
                    CloseStream(pfd[2*(i-1)+1]);
                }else{
                    if(source>=0){
                        CloseStream(STDIN);
                        DupStream(npipes[source][0],STDIN);
                    }
                }
                //除了最後一個process之外都將output寫到pipe
                if(!(i==pipenum)){
                    CloseStream(STDOUT);
                    DupStream(pfd[2*i+1],STDOUT);
                    CloseStream(pfd[2*i]);
                //如果最後要寫到file
                }else{
                    if(tofile){
                        CloseStream(STDOUT);
                        DupStream(f,STDOUT);
                        CloseStream(f);
                    }else if(topipe){
                        CloseStream(STDOUT);
                        DupStream(npipes[destination][1],STDOUT);
                    }else if(toepipe){
                        CloseStream(STDERR);
                        DupStream(npipes[destination][1],STDERR);
                        CloseStream(STDOUT);
                        DupStream(npipes[destination][1],STDOUT);
                    }
                }
                CloseNumberPipes(source,destination);
                Launch(arg[0],arg);
            }
        }
        //parent process將最後一個pipe關掉
        if(pipenum>0){
            CloseStream(pfd[2*pipenum-2]);
            CloseStream(pfd[2*pipenum-1]);
        }
        delete[] pfd;
    }
    //需要輸出到檔案
    else if(line.find(">")!=string::npos){
        int f; 
        int *temp = new int[2];
        commands=SplitString(line,">");
        RemoveSpace(commands);
        arg=VectorToChar(SplitString(commands[0]," "));
        source=SetInput();
        ForkNewProcess(pid);
        pids.push(pid);
        if(pid==0){
            if(source>=0){
                CloseStream(STDIN);
                DupStream(npipes[source][0],STDIN);
            }
            CloseNumberPipes(source,-1);
            f=open(commands[1].c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
            DupStream(f,STDOUT);
            CloseStream(f);
            Launch(arg[0],arg);
        }

    //只有單一command
    }else{
        //分割指令
        commands=SplitString(line," ");
        if(commands[0]=="exit"){
           exit(EXIT_SUCCESS);
        }else if(commands[0]=="setenv"){
            SEnv(commands[1],commands[2]);
        }else if(commands[0]=="printenv"){
            PEnv(commands[1]);
        }else{
            //將vector轉為char*後return char**
            arg=VectorToChar(commands);
            //fork porcess
            source=SetInput();
            ForkNewProcess(pid);  
            pids.push(pid);
            if(pid==0){
                //child process
                if(source>=0){
                    CloseStream(STDIN);
                    DupStream(npipes[source][0],STDIN);
                }
                CloseNumberPipes(source,-1);
                Launch(arg[0],arg);
            }
        }
    }
    //parent將用完的number pipe關掉
    for(int i=0;i<npipes.size();i++){
        if(pipedests[i]==0){
            CloseStream(npipes[i][0]);
            CloseStream(npipes[i][1]);
            delete[] npipes[i];
            npipes.erase(npipes.begin()+i);
            pipedests.erase(pipedests.begin()+i);
        }
    }
    while(!(pids.empty())){
        int status;
        waitpid(pids.front(),&status,0);
        pids.pop();
    }
}

int main(){
    string line;
    //設定PATH
    setenv("PATH","bin:.",1);
    while(1){
        //將目前PATH中的指令加入buildin set
        SetBuildin();
        //command line prompt
        cout<< "% ";
        //讀取指令
        line=ReadLine();
        if(line!=""){
            ParseLine(line);
        }
    }
}
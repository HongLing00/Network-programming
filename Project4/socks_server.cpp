#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <errno.h>
#include <vector>
#include <time.h>  
using namespace std;

enum { max_length = 60000 };

void ChildHandler(int signo){
    int status;
    while(waitpid(-1,&status,WNOHANG)>0){}
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

void CloseStream(int target){
    if(close(target)<0){
        cerr<<"close error("<<target<<")-"<<strerror(errno)<<endl;
    }
}

class ConnectSession{
    private:
        int webfd , browserfd ,datalength;
        char data[max_length];
        unsigned int IP , PORT;
        struct sockaddr_in server;

    public:
        ConnectSession(int socket, unsigned int ip, unsigned int port):browserfd(socket),IP(ip),PORT(port){start();}
    private:
        void start(){
            // cout<<"start connect"<<endl;
            webfd = socket(AF_INET,SOCK_STREAM,0);
            memset (&server, 0, sizeof (server));
            server.sin_family = AF_INET;
            server.sin_addr.s_addr = IP;
            server.sin_port = htons(PORT);
            do_connect();
        }

        void do_connect(){
            if(connect(webfd,(struct sockaddr *)&server,sizeof(server)) <0){
                cerr<<"Connect to web failed"<<endl;
                exit(EXIT_FAILURE);
            }
            do_select();
        }

        void do_select(){
            // cout<<"start select"<<endl;
            fd_set rfds,afds;
            int nfds = max(browserfd,webfd) +1;
            FD_ZERO(&afds);
            FD_SET(webfd,&afds);
            FD_SET(browserfd,&afds);
            while(1){
                FD_ZERO(&rfds);
                rfds = afds;
                if(select(nfds,&rfds,NULL,NULL,NULL) >= 0) {
                    if(FD_ISSET(browserfd,&rfds)){
                        memset(data,0,max_length);
                        datalength = read(browserfd,data,max_length);
                        // cout<<"read "<<datalength<<" from browser"<<endl;
                        if(datalength == 0){
                            // cout<<"browserfd end"<<endl;
                            exit(EXIT_SUCCESS);
                        }else if(datalength == -1){
                            // cout<<"read browserfd error"<<endl;
                            exit(EXIT_FAILURE);
                        } else {
                            write(webfd,data,datalength);
                        }   
                    }
                    if(FD_ISSET(webfd,&rfds)){
                        memset(data,0,max_length);
                        datalength = read(webfd,data,max_length);
                        // cout<<"read "<<datalength<<" from web"<<endl;
                        if(datalength == 0){
                            // cout<<"webfd end"<<endl;
                            exit(EXIT_SUCCESS);
                        }else if(datalength == -1){
                            cerr<<"read webfd error"<<endl;
                            exit(EXIT_FAILURE);
                        }else{
                            write(browserfd,data,datalength);
                        }
                    }
                }
            }
        }
};

class BindSession{
    private:
        int ftpfd,browserfd,bindfd,datalength,bindport;
        char data[max_length];
        unsigned char reply[8];
        struct sockaddr_in bind_address;

    public:
        BindSession(int socket) : browserfd(socket) {
            start();
        }

    private:
        void start() {
            bindfd = socket(AF_INET,SOCK_STREAM,0);
            if(bindfd<0){
                cerr<<"bind error : "<<strerror(errno)<<endl;
                exit(EXIT_FAILURE);
            }
            bind_address.sin_family = AF_INET;
            bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
            bind_address.sin_port = htons(INADDR_ANY);
            do_bind();
        }

        void do_bind(){
            // cout<<"port "<<bind_address.sin_port<<" "<<htons(bindport)<<" "<<bindport<<endl;
            bind(bindfd, (struct sockaddr *) &bind_address, sizeof(bind_address)) ;
            do_reply();
        }

        void do_reply(){
            struct sockaddr_in sa;
            socklen_t sa_len = sizeof(sa);
            if(getsockname(bindfd,(struct sockaddr*) &sa,&sa_len)<0){
                cerr<<"getsockname error : "<<endl;
            }
            if(listen(bindfd,5) < 0){
                cerr<<"listen : can't listen"<<endl;
            }
            reply[0] = 0;
            reply[1] = 0x5A;
            reply[2] = (unsigned char) (ntohs(sa.sin_port)/256);
            reply[3] = (unsigned char) (ntohs(sa.sin_port)%256);
            for(int i=4;i<8;++i) reply[i] = 0;
            write(browserfd,reply,8);
            do_accept();
        }

        void do_accept() {
            struct sockaddr_in ftp_address;
            socklen_t ftp_len = sizeof(ftp_address);
            ftpfd = accept(bindfd,(struct sockaddr*) &ftp_address, (socklen_t*) &ftp_len);
            if(ftpfd < 0){
                cerr<<"ftpfd : accept error : "<<endl;
                exit(EXIT_FAILURE);
            }
            write(browserfd,reply,8);
            do_select();
        }
        
        void do_select(){
            fd_set afds,rfds;
            int nfds = max(ftpfd,browserfd) +1;
            FD_ZERO(&afds);
            FD_SET(ftpfd,&afds);
            FD_SET(browserfd,&afds);
            while(1){
                FD_ZERO(&rfds);
                rfds = afds;
                if(select(nfds,&rfds,NULL,NULL,NULL) < 0){
                }else{
                    if(FD_ISSET(browserfd,&rfds)){
                        memset(data,0,max_length);
                        datalength = read(browserfd,data,max_length);
                        // cout<<"Upload : "<<datalength<<" to ftp"<<endl;
                        if(datalength == 0){
                            // cout<<"browserfd end"<<endl;
                            exit(EXIT_SUCCESS);
                        } else if(datalength == -1){
                            cerr<<"read browserfd error"<<endl;
                            exit(EXIT_FAILURE);
                        } else {
                            write(ftpfd,data,datalength);
                        }
                    }
                    if(FD_ISSET(ftpfd,&rfds)){
                        memset(data,0,max_length);
                        datalength = read(ftpfd,data,max_length);
                        // cout<<"Download : "<<datalength<<" from ftp"<<endl;
                        if(datalength == 0){
                            // cout<<"ftpfd end"<<endl;
                            exit(EXIT_SUCCESS);
                        } else if(datalength == -1){
                            cerr<<"read ftpfd error"<<endl;
                            exit(EXIT_SUCCESS);
                        } else {
                            write(browserfd,data,datalength);
                        }
                    }
                }
            }
        }

};

class Socks4Session{
    private:
        unsigned char VN , CD , addr[4] ,reply[8],request[max_length];
        unsigned int DSTPORT , DSTIP;
        int browserfd,request_num;
        struct sockaddr_in client;
        ifstream firewall;
        ifstream cfirewall;
        bool deny;
    public:
        Socks4Session(int socket,const struct sockaddr_in& cli) : browserfd(socket),client(cli),firewall("socks.conf"),cfirewall("client.conf") {
            do_read();
        }

    private:
        void do_read() {
            request_num=read(browserfd,request,max_length);
            if(request_num<=0){
                exit(EXIT_SUCCESS);
            }
            for(int i=0;i<request_num;i++){
                cout<<i<<" "<<to_string(request[i])<<endl;
            }
            VN = request[0];
            CD = request[1];
            DSTPORT =request[2] << 8 | request[3];
            DSTIP = request[7] << 24 | request[6] << 16 | request[5] << 8 | request[4];
            cout<<"VN : "<<to_string(VN)<<" , CD : "<<to_string(CD)<<" , DSTPORT : "<<DSTPORT<< " , DSTIP : "<<DSTIP<<endl;
            if(VN != 0x04){
                cout<<"Not socks4 request\n";
                exit(EXIT_FAILURE);
            }
                        char address[20];
            inet_ntop(AF_INET, &client.sin_addr, address, sizeof(address));
            deny=false;
//===========================================================================
            string cline="";
            vector <string> clientv;
            vector <string> caddr_str;
            vector <string> sip;
            sip=SplitString(address,".");

            while(1){
                getline(cfirewall,cline);
                clientv = SplitString(cline," ");
                caddr_str = SplitString(clientv[2],".");
                for(int i=0; i<clientv.size();i++){
                    cout<<clientv[i]<<endl;

                }
                for(int i=0; i<caddr_str.size();i++){
                    cout<<caddr_str[i]<<endl;

                }
                for(int i=0;i<sip.size();i++){
                    cout<<i<<"-"<<sip[i]<<endl;

                }
cout<<((clientv[1] == "c" && CD == 0x01) || (clientv[1] == "b" && CD == 0x02))<<endl;
cout<<(((caddr_str[0] == sip[0]) || (caddr_str[0] == "*")) && ((caddr_str[1] == sip[1]) || (caddr_str[1] == "*")))<<endl;
cout<<(((caddr_str[2] == sip[2]) || (caddr_str[2] == "*")) && ((caddr_str[3] == sip[3]) || (caddr_str[3] == "*")))<<endl;

                if((clientv[1] == "c" && CD == 0x01) || (clientv[1] == "b" && CD == 0x02)){//is connect/bind mode
                    if(((caddr_str[0] == sip[0]) || (caddr_str[0] == "*")) && ((caddr_str[1] == sip[1]) || (caddr_str[1] == "*"))
                        && ((caddr_str[2] == sip[2]) || (caddr_str[2] == "*")) && ((caddr_str[3] == sip[3]) || (caddr_str[3] == "*"))){
                            cout<<"Client firewall accept with rule [ "<<clientv[0]<<" "<<clientv[1]<<" "<<caddr_str[0]<<"."<<caddr_str[1]<<"."<<caddr_str[2]<<"."<<caddr_str[3]<<" ]"<<endl;
                            break;
                    }
                }
                if(cfirewall.eof()){

                    cout<<"Client firewall reject with rule [ "<<clientv[0]<<" "<<clientv[1]<<" "<<caddr_str[0]<<"."<<caddr_str[1]<<"."<<caddr_str[2]<<"."<<caddr_str[3]<<" ]"<<endl;
                                       exit(EXIT_SUCCESS);
                    break;
                }
            }
//===========================================================================
                string line="";
                vector <string> dest;
                vector <string> addr_str;


                while(1){
                    getline(firewall,line);
                    dest = SplitString(line," ");
                    addr_str = SplitString(dest[2],".");
                    for(int i=0;i<4;i++){
                        addr[i] = addr_str[i]=="*" ? 0x00 : (unsigned char) stoi(addr_str[i]);
                    }
                    if((dest[1] == "c" && CD == 0x01) || (dest[1] == "b" && CD == 0x02)){//is connect/bind mode
                        if(((addr[0] == request[4]) || (addr[0] == 0)) && ((addr[1] == request[5]) || (addr[1] == 0x00))
                            && ((addr[2] == request[6]) || (addr[2] == 0x00)) && ((addr[3] == request[7]) || (addr[3] == 0x00))){
                                cout<<"Firewall accept with rule [ "<<dest[0]<<" "<<dest[1]<<" "<<addr_str[0]<<"."<<addr_str[1]<<"."<<addr_str[2]<<"."<<addr_str[3]<<" ]"<<endl;
                                reply[1] = 0x5A;//let CD = 90
                                break;
                        }
                    }
                    if(firewall.eof()){
                        reply[1] = 0x5B;//let CD = 91
                        break;
                    }
                }


            cout<<"===================================================================================="<<endl;

            cout<<"S_IP : "<<address<<endl;
            cout<<"S_PORT : "<<ntohs(client.sin_port)<<endl;
            cout<<"DST_IP : "<<(int)request[4]<<'.'<<(int)request[5]<<'.'<<(int)request[6]<<'.'<<(int)request[7]<<endl;
            cout<<"DST_PORT : "<<DSTPORT<<endl;
            cout<<"Command : "<<(CD==0x01?"CONNECT":"BIND")<<endl;
            cout<<"Reply : "<<(reply[1]==0x5A?"Accept":"Reply")<<endl;
            cout<<"===================================================================================="<<endl;
            if(reply[1] == 0x5B){
                reply[0] = 0;
                for(int i=2;i<8;++i) reply[i] = request[i];
                write(browserfd,reply,8);
                CloseStream(browserfd);
                exit(EXIT_SUCCESS);
            }
            if(CD==0x01){
                reply[0] = 0;
                for(int i=2;i<8;i++) reply[i] = request[i];
                write(browserfd,reply,8);
                ConnectSession conn(browserfd,DSTIP,DSTPORT);
            }else if(CD==0x02){
                BindSession bin(browserfd);
            }
        }
};

class Socks4Server{
    private:
        int listener, sockfd;
        struct sockaddr_in client,server;
        short PORT;
        pid_t pid;
    public:
        Socks4Server(short port):PORT(port){ start(); }

    private:
        void start() {
            listener=socket(AF_INET, SOCK_STREAM,0);
                if (listener<0){
                    cerr<<"Create socket error: "<<strerror(errno)<<endl;
                    exit(EXIT_FAILURE);
                }
            do_setting();
        }

        void do_setting(){
            memset (&server, 0, sizeof (server)) ;
            server.sin_family = AF_INET;
            server.sin_port = htons(PORT);
            server.sin_addr.s_addr = htonl(INADDR_ANY);
            int opt_val = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);
            do_bind();
        }

        void do_bind(){
            bind(listener,(struct sockaddr *)&server,sizeof(server));
            do_listen();
        }

        void do_listen(){
            if(listen(listener,5)<0){
                cerr<<"listen error: "<<strerror(errno)<<endl;
                exit(EXIT_FAILURE);
            }
            do_accept();
        }

        void do_accept(){
            while(1) {
                socklen_t addrlen = sizeof(client);
                sockfd = accept(listener, (struct sockaddr *) &client, &addrlen);
                cout<<"accept : "<<sockfd<<endl;
                if(sockfd>=0){
                    pid=fork();
                    if(pid==0){
                        cout<<"close listenfd in child\n";
                        CloseStream(listener);
                        Socks4Session session(sockfd,client);
                        exit(0);
                    }else if(pid>0){
                        CloseStream(sockfd);
                    }else{
                        cerr<<"Could not establish new connection :"<<strerror(errno)<<endl;
                    }
                }
            }
        }
};

int main(int argc ,char* const argv[]){
    if (argc != 2) {
        std::cerr << "Usage:" << argv[0] << " [port]" << endl;
        return 1;
    }
    signal(SIGCHLD,ChildHandler);
    try {
        short port = atoi(argv[1]);
        Socks4Server server(port);
    }catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}
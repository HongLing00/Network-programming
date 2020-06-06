#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <map>
#include <vector>
#include <fstream>

using namespace std;
using namespace boost::asio;

io_service global_io_service;
const string httpok ="HTTP/1.1 200 OK";
map<int,map <string,string> > info;
enum { max_length =60000};

extern char **environ;//所有的環境變數

string escape(string data){
    string session = "" ;
    for ( auto&& ch : data) session += ( "&#" + to_string(int(ch)) + ";" );
    return session;
}

void OutputShell(string session , string c){
    string content=escape(c);
    cout<<"<script>document.getElementById('"<<session<<"').innerHTML += '"<<content<<"';</script>"<<endl;
}

void OutputCommand(string session , string c){
    string content=escape(c);
    cout<<"<script>document.getElementById('"<<session<<"').innerHTML += '<b>"<<content<<"</b>';</script>"<<endl;    
}

void PrintConsoleTable(){
    cout<<"Content-type: text/html"<<"\r\n\r\n";
    cout<<"<!DOCTYPE html>" <<endl;
    cout<<"<html lang=\"en\">" <<endl;
    cout<<  "<head>" <<endl;
    cout<<      "<meta charset=\"UTF-8\" />" <<endl;
    cout<<      "<title>NP Project 3 Console</title>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "rel=\"stylesheet\"" <<endl;
    cout<<          "href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\"" <<endl;
    cout<<          "integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\"" <<endl;
    cout<<          "crossorigin=\"anonymous\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"" <<endl;
    cout<<          "rel=\"stylesheet\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<link" <<endl;
    cout<<          "rel=\"icon\"" <<endl;
    cout<<          "type=\"image/png\"" <<endl;
    cout<<          "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"" <<endl;
    cout<<      "/>" <<endl;
    cout<<      "<style>" <<endl;
    cout<<          "* {" <<endl;
    cout<<              "font-family: 'Source Code Pro', monospace;" <<endl;
    cout<<              "font-size: 1rem !important;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "body {" <<endl;
    cout<<              "background-color: #212529;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "pre {" <<endl;
    cout<<              "color: #87CEFA;" <<endl;
    cout<<          "}" <<endl;
    cout<<          "b {" <<endl;
    cout<<              "color: #FFB7DD;" <<endl;
    cout<<          "}" <<endl;
    cout<<      "</style>" <<endl;
    cout<<  "</head>" <<endl;
    cout<<  "<body>" <<endl;
    cout<<      "<table class=\"table table-dark table-bordered\">" <<endl;
    cout<<          "<thead>" <<endl;
    cout<<              "<tr>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[0]["HOST"]<<":"<<info[0]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[1]["HOST"]<<":"<<info[1]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[2]["HOST"]<<":"<<info[2]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[3]["HOST"]<<":"<<info[3]["PORT"]<<"</th>" <<endl;
    cout<<                  "<th scope=\"col\">"<<info[4]["HOST"]<<":"<<info[4]["PORT"]<<"</th>" <<endl;
    cout<<              "</tr>" <<endl;
    cout<<          "</thead>" <<endl;
    cout<<          "<tbody>" <<endl;
    cout<<              "<tr>" <<endl;
    cout<<                  "<td><pre id=\"s0\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s1\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s2\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s3\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<                  "<td><pre id=\"s4\" class=\"mb-0\"></pre></td>" <<endl;
    cout<<              "</tr>" <<endl;
    cout<<          "</tbody>" <<endl;
    cout<<      "</table>" <<endl;
    cout<<  "</body>" <<endl;
    cout<<"</html>" <<endl;
}

string GEnv(string name){
    char* value=getenv(name.c_str());
    if(value!=NULL){
        return value;
    }
    return "";
}

void SEnv(string name ,string value){
    setenv(name.c_str(),value.c_str(),1);
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

void SetConnectionInfo(){
    string qstr=GEnv("QUERY_STRING");
    vector<string> env = SplitString(qstr,"&");
    int pos=0;
    for(int i=0;i<17;i++){
        pos=env[i].find("=");
        if(i<15){
            if((i%3)==0){
                info[(i/3)]["HOST"]= pos<env[i].length()-1 ? env[i].substr(pos+1) : "" ;
            }else if((i%3)==1){
                info[(i/3)]["PORT"]= pos<env[i].length()-1 ? env[i].substr(pos+1) : "" ;
            }else{
                info[(i/3)]["FILE"]= pos<env[i].length()-1 ? "test_case/"+env[i].substr(pos+1) : "" ;
            }
        }else{
            if(pos<env[i].length()-1){
                info[(5)][(i==15?"HOST":"PORT")]=env[i].substr(pos+1);
            }else{
                cerr<<"Please set the "<<(i==15?"HOST":"PORT")<<" of socks server"<<endl;
                exit(EXIT_SUCCESS);
            }
        }
    }
}

class Session : public enable_shared_from_this<Session> {
    private:
        array<char, max_length> _data;
        array<unsigned char, max_length> reply;
        string data;
        ip::tcp::resolver _resolver;
        int ID;
        string session;
        ifstream testfile;
        ip::tcp::socket Socket;
    public:
        Session(int id,ip::tcp::socket soc) : 
            _resolver(global_io_service),ID(id),
            session("s"+to_string(id)),testfile(info[id]["FILE"]),Socket(move(soc)){}

        void start() {
            Resolve();
        }

    private:
        void Resolve(){
            auto self(shared_from_this());
            _resolver.async_resolve( ip::tcp::resolver::query(info[ID]["HOST"],info[ID]["PORT"]),
                [this,self](boost::system::error_code ec,ip::tcp::resolver::iterator iterator) {
                    if (!ec) {
                        SendSocks4package(iterator);
                    }else{
                        OutputShell(session,ec.message());
                    }
                }
            );
        }

        void SendSocks4package(ip::tcp::resolver::iterator iterator) {
            auto self(shared_from_this());
            ip::tcp::endpoint ep=*iterator;
            string ip=ep.address().to_string();
            vector <string> IP =SplitString(ip,".");
            array<unsigned char, 9> request;
            request[0]=(unsigned char) 4;
            request[1]=(unsigned char) 1;
            request[2]=(unsigned char) (ep.port()/256);
            request[3]=(unsigned char) (ep.port()%256);
            request[4]=(unsigned char) stoi(IP[0]);
            request[5]=(unsigned char) stoi(IP[1]);
            request[6]=(unsigned char) stoi(IP[2]);
            request[7]=(unsigned char) stoi(IP[3]);
            request[8]=(unsigned char) 0;
            
            async_write(Socket,buffer(request,9),
                [this,self](boost::system::error_code ec, std::size_t length) {
                    if (!ec) {
                        ReadSocks4Package();
                    }else{
                        OutputShell(session,ec.message());
                    }
                }
            );
        }
        void ReadSocks4Package(){
            auto self(shared_from_this());
            Socket.async_read_some(buffer(reply, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if(!ec){
                    //     for(int i=0;i<length;i++){
                    //         OutputShell(session,to_string(i)+"-"+to_string(reply[i])+"\n");
                    //     }
                        do_read();
                    }else{
                        OutputShell(session,ec.message());
                    }
                }
            );
        }

        void do_read() {
            auto self(shared_from_this());
            Socket.async_read_some(buffer(_data, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if(!ec){
                        data="";
                        for(int i=0;i<length;i++){
                            data+=_data[i];
                        }
                        OutputShell(session,data);
                        if(data.find("%")!=string::npos){
                                do_write();
                        }
                        do_read();
                    }else{
                        OutputShell(session,ec.message());
                    }
                }
            );
        }

        void do_write() {
            auto self(shared_from_this());
            string line="";
            getline(testfile,line);
            line += '\n';
            OutputCommand(session,line);
            Socket.async_write_some(buffer(line.c_str(), line.length()),
                [this, self](boost::system::error_code ec, std::size_t length ) {
                    if(!ec) {
                        do_read();
                    }else{
                        OutputShell(session,ec.message());
                    }
                }
            );
            if(line=="exit\n"){
                testfile.close();
                Socket.close();
                exit(EXIT_SUCCESS);
            }
        }
};

class Client  {
    private:
        ip::tcp::socket _socket;
        ip::tcp::resolver _resolver;
        int ID;
    public:
        Client(int i) : 
            _socket(global_io_service) ,_resolver(global_io_service),ID(i){do_resolve();}

    private:
        void do_resolve(){
            _resolver.async_resolve( ip::tcp::resolver::query(info[5]["HOST"],info[5]["PORT"]),
                [this](boost::system::error_code ec,ip::tcp::resolver::iterator iterator) {
                    if (!ec) {
                        // ip::tcp::endpoint ep=*iterator;
                        // OutputShell("s"+to_string(ID),info[5]["HOST"]+"\n");
                        // OutputShell("s"+to_string(ID),info[5]["PORT"]+"\n");
                        // OutputShell("s"+to_string(ID),ep.address().to_string()+"\n");
                        // OutputShell("s"+to_string(ID),to_string(ep.port())+"\n");
                        do_connect(iterator);
                    }else{
                        OutputShell("s"+to_string(ID),ec.message());
                    }
                }
            );
        }

        void do_connect(ip::tcp::resolver::iterator iterator) {
            _socket.async_connect( *iterator,
                [this](boost::system::error_code ec) {
                    if (!ec) {
                        make_shared<Session>(ID,move(_socket))->start();
                    }else{
                        OutputShell("s"+to_string(ID),ec.message());
                    }
                }
            );
        }
};

int main(){
    SetConnectionInfo();
    PrintConsoleTable();
    try {
        for(int i=0;i<5;i++){
            if(info[i]["HOST"]!=""){
                if(fork()==0){
                    Client client(i);
                    global_io_service.run();
                }
            }
        }

    }catch (exception& e) {
        cerr << "Exception: " << e.what()  << "\n";
    }
}
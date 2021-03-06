#include "Server.h"
#include<iostream>

Server::Server()
{
	_serverSock = INVALID_SOCKET;
	_recvCount = 0;//!一定要初始化
	_clientCount = 0;//!一定要初始化
	_msgCount = 0;//!一定要初始化

	_addressFamily = AF_INET;

	_nSendBufSize = CellConfig::Instance().getInt("nSendBufSize", SEND_BUF_SIZE);
	_nRecvBufSize = CellConfig::Instance().getInt("nRecvBufSize", RECV_BUF_SIZE);
	_nMaxClient = CellConfig::Instance().getInt("nClient", FD_SETSIZE);
}

Server::~Server()
{
	CloseServer();
}

//初始化
SOCKET Server::InitServer(int af)
{
	_addressFamily = af;

	CellNetwork::Init();
	if (_serverSock != INVALID_SOCKET)
	{
		CELLLOG_WARRING("initSocket close old socket<%d>...", (int)_serverSock);
		CloseServer();
	}

	_serverSock = socket(af, SOCK_STREAM, IPPROTO_TCP);
	if (_serverSock == INVALID_SOCKET)
	{
		CELLLOG_PERROR("create socket failed...");
	}
	else
	{
		CellNetwork::make_reuseaddr(_serverSock);
		CELLLOG_INFO("create socket<%d> success...", (int)_serverSock);
	}
	return _serverSock;
}

//绑定ip地址和端口，参数为ip地址 ip，端口 port
int Server::Bind(const char* ip, unsigned short port)
{
	int ret = SOCKET_ERROR;

	if (_addressFamily == AF_INET) {//ipv4
		sockaddr_in server_addr = {};
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(port);

#ifdef _WIN32
		if (ip)
			server_addr.sin_addr.S_un.S_addr = inet_addr(ip);
		else
			server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
#else
		if (ip)
			server_addr.sin_addr.s_addr = inet_addr(ip);
		else
			server_addr.sin_addr.s_addr = INADDR_ANY;
#endif // _WIN32

		ret = bind(_serverSock, (sockaddr*)& server_addr, sizeof(server_addr));
	}
	else if (_addressFamily == AF_INET6) {//ipv6
		sockaddr_in6 server_addr = {};
		server_addr.sin6_family = AF_INET6;
		server_addr.sin6_port = htons(port);
		if (ip) {
			inet_pton(AF_INET, ip, &server_addr.sin6_addr);
		}
		else {
			server_addr.sin6_addr = in6addr_any;
		}
		ret = bind(_serverSock, (sockaddr*)& server_addr, sizeof(server_addr));
	}
	else {
		CELLLOG_PERROR("bind _addressFamily failed,...", _addressFamily);
	}

	if (ret == SOCKET_ERROR)
		CELLLOG_PERROR("bind port<%d> failed...", port);
	else
		CELLLOG_INFO("bind port<%d> success...", port);
	return ret;
}

//监听端口号
int Server::Listen(int n)
{
	int ret = listen(_serverSock, n);
	if (ret == SOCKET_ERROR) {
		CELLLOG_PERROR("listen socket<%d> failed...", (int)_serverSock);
	}
	else {
		CELLLOG_INFO("listen port<%d> success...", (int)_serverSock);
	}
	return ret;
}

//接受客户端连接
SOCKET Server::Accept()
{
	if (_addressFamily == AF_INET)
		return AcceptIPV4();
	if (_addressFamily == AF_INET6)
		return AcceptIPV6();
}

SOCKET Server::AcceptIPV4()
{
	SOCKET clientSock = INVALID_SOCKET;
	sockaddr_in client_addr;

#ifdef _WIN32
	int client_addr_size = sizeof(client_addr);
#else
	socklen_t client_addr_size = sizeof(client_addr);
#endif // _WIN32

	clientSock = accept(_serverSock, (sockaddr*)& client_addr, &client_addr_size);
	if (clientSock == INVALID_SOCKET)
	{
		CELLLOG_PERROR("<socket=%d> accept error.", (int)_serverSock);
	}
	else
	{
		//当前连接的客户端数量小于最多可以连接的数目
		if (_clientCount < _nMaxClient)
		{
			CellNetwork::make_reuseaddr(clientSock);
			AddClientToCellServer(new CellClient(clientSock, _nSendBufSize, _nRecvBufSize));//将新客户端分配给客户数量最少的cellServer
		}
		else
		{
			//超出最大连接数目，直接关闭该客户端
			CellNetwork::DestorySocket(clientSock);
			CELLLOG_WARRING("Accept to nMaxClient");
		}
	}
	return clientSock;
}

SOCKET Server::AcceptIPV6()
{
	SOCKET clientSock = INVALID_SOCKET;
	sockaddr_in6 client_addr;

#ifdef _WIN32
	int client_addr_size = sizeof(client_addr);
#else
	socklen_t client_addr_size = sizeof(client_addr);
#endif // _WIN32

	clientSock = accept(_serverSock, (sockaddr*)& client_addr, &client_addr_size);
	if (clientSock == INVALID_SOCKET)
	{
		CELLLOG_PERROR("<socket=%d> accept error.", (int)_serverSock);
	}
	else
	{
		CellNetwork::make_reuseaddr(clientSock);

		//获取ip地址
		static char ip[INET6_ADDRSTRLEN] = {};
		inet_ntop(AF_INET6, &client_addr.sin6_addr, ip, INET6_ADDRSTRLEN - 1);
		CELLLOG_INFO("Accept_IP: %s", ip);

		//当前连接的客户端数量小于最多可以连接的数目
		if (_clientCount < _nMaxClient)
		{
			CellNetwork::make_reuseaddr(clientSock);
			AddClientToCellServer(new CellClient(clientSock, _nSendBufSize, _nRecvBufSize));//将新客户端分配给客户数量最少的cellServer
		}
		else
		{
			//超出最大连接数目，直接关闭该客户端
			CellNetwork::DestorySocket(clientSock);
			CELLLOG_WARRING("Accept to nMaxClient");
		}
	}
	return clientSock;
}

//将新客户端分配给客户数量最少的cellServer，参数为客户端 pClient
void Server::AddClientToCellServer(CellClient* pClient)
{
	//查找客户数量最少的CellServer消息处理对象
	auto pMinCellServer = _cellServers[0];
	for (auto pCellServer : _cellServers)
	{
		if (pCellServer->GetClientCount() < pMinCellServer->GetClientCount())
		{
			pMinCellServer = pCellServer;
		}
	}
	pMinCellServer->AddClient(pClient);//加入客户端数量最小的消息处理线程
}

//关闭
void Server::CloseServer()
{
	CELLLOG_INFO("Server.Close begin");
	_thread.closeThread();//关闭新客户端连接处理线程

	//避免重复关闭！
	if (_serverSock != INVALID_SOCKET)
	{
		for (auto cs : _cellServers)
		{
			delete cs;
		}
		_cellServers.clear();

		CellNetwork::DestorySocket(_serverSock);
		_serverSock = INVALID_SOCKET;
	}
	CELLLOG_INFO("Server.Close end");
}

//客户端加入事件
void Server::onNetJoin(CellClient* pClient)
{
	++_clientCount;
}

//客户端离开事件
void Server::onNetLeave(CellClient* pClient)
{
	--_clientCount;
}

//消息统计事件
void Server::onNetMsg(CellServer* pCellServer, CellClient* pClient, netmsg_DataHeader* header)
{
	++_msgCount;
}

//消息接收统计事件
void Server::onNetRecv(CellClient* pClient)
{
	++_recvCount;
}

//计算并输出每秒收到的网络消息
void Server::Time4Msg()
{
	auto t1 = _tTime.getElapsedSecond();
	if (t1 >= 1.0)
	{
		CELLLOG_INFO("thread<%d>,time<%lf>,socket<%d>,clients<%d>,recvCount<%d>,msgCount<%d>",
			(int)_cellServers.size(), (double)t1, (int)_serverSock,
			(int)(_clientCount), (int)(_recvCount), (int)_msgCount);
		_recvCount = 0;
		_msgCount = 0;
		_tTime.update();
	}
}

//返回服务器的socket描述符
SOCKET Server::GetServerSock()
{
	return _serverSock;
}


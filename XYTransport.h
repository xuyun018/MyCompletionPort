#ifndef XYTransportH
#define XYTransportH
//---------------------------------------------------------------------------
#pragma pack(push)
#include <winsock2.h>
#pragma pack(pop)
#include <mswsock.h>
#include <mstcpip.h>
#include <windows.h>
#include <tchar.h>

#include <stdio.h>

#include "XYDebug.h"

#ifndef XYDYNAMIC_LOAD
#define XYDYNAMIC_LOAD
#endif

#ifndef XYTCP_NODELAY
#define XYTCP_NODELAY
#endif

#ifndef XYFIND_BUG
//#define XYFIND_BUG
#endif
#ifndef XYDEBUG
//#define XYDEBUG
#endif
#ifndef XYDEBUG_TINY
//#define XYDEBUG_TINY
#endif
//---------------------------------------------------------------------------
#ifndef XYDYNAMIC_LOAD
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"mswsock.lib")
#else
typedef int (WINAPI *t_WSAGetLastError)(void);
typedef int (WINAPI *t_WSAIoctl)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WINAPI *t_WSASendTo)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const struct sockaddr *, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WINAPI *t_WSARecvFrom)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, struct sockaddr *, LPINT, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WINAPI *t_WSASend)(SOCKET ,LPWSABUF ,DWORD ,LPDWORD ,DWORD ,LPWSAOVERLAPPED ,LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WINAPI *t_WSARecv)(SOCKET ,LPWSABUF ,DWORD ,LPDWORD ,LPDWORD ,LPWSAOVERLAPPED ,LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef SOCKET(WINAPI *t_WSASocket)(int, int, int, LPWSAPROTOCOL_INFO, GROUP, DWORD);
typedef int (WINAPI *t_closesocket)(SOCKET);
typedef int (WINAPI *t_shutdown)(SOCKET, int);
typedef u_short(WINAPI *t_htons)(u_short);
typedef unsigned long (WINAPI *t_inet_addr)(const char *);
typedef int (WINAPI *t_setsockopt)(SOCKET, int, int, const char *, int);
typedef int (WINAPI *t_bind)(SOCKET ,const struct sockaddr *,int);
typedef int (WINAPI *t_listen)(SOCKET ,int);
#endif
//---------------------------------------------------------------------------
// 设计让调用者去回收TCP连接结点
//---------------------------------------------------------------------------
//#define XYTRANSPORT_RETURN_FLAG_SEND											0x01

#define XYTRANSPORT_UDP_RELEASE													0
#define XYTRANSPORT_UDP_RECV													1
#define XYTRANSPORT_UDP_SEND													2

#define XYTRANSPORT_TCP_CLOSE													3
#define XYTRANSPORT_TCP_CONNECT													4
#define XYTRANSPORT_TCP_RECV													5
#define XYTRANSPORT_TCP_SEND0													6
#define XYTRANSPORT_TCP_SEND1													7

#define XYTRANSPORT_ERROR_ABORT0												1
#define XYTRANSPORT_ERROR_ABORT1												2
//---------------------------------------------------------------------------
typedef UINT(CALLBACK *XYTRANSPORT_PROCEDURE)(LPVOID, LPVOID, LPVOID, LPVOID *, LPVOID, LPVOID, BYTE, const char *, int);
//---------------------------------------------------------------------------
typedef struct tagXYTCP_HOST_INFO
{
	LPVOID listener;

	LPVOID customdata;

	SOCKADDR_IN *lplocaladdress;
	SOCKADDR_IN *lpremoteaddress;

	int locallength;
	int remotelength;
}XYTCP_HOST_INFO, *PXYTCP_HOST_INFO;

typedef struct tagXYTRANSPORT
{
	HANDLE heap;

	// UDP
	LPVOID stack0;
	LPVOID stack1;

	LPVOID udp_head;
	//

	// TCP
	LPVOID stacks[3];
	LPVOID heads[2];

	LPVOID stack;

	LPVOID head;
	LPVOID rear;

	UINT sequence;
	//

	BOOL working;

	HANDLE *hthreads;
	UINT count;

	HANDLE hcompletion;

	DWORD bufferlength;

	DWORD contextsize1;
	DWORD contextsize;

	LPVOID parameter;

	XYTRANSPORT_PROCEDURE procedure;

#ifdef XYDYNAMIC_LOAD
	t_WSAGetLastError pfn_WSAGetLastError;
	t_WSAIoctl pfn_WSAIoctl;
	t_WSASendTo pfn_WSASendTo;
	t_WSARecvFrom pfn_WSARecvFrom;
	t_WSASend pfn_WSASend;
	t_WSARecv pfn_WSARecv;
	t_WSASocket pfn_WSASocket;
	t_closesocket pfn_closesocket;
	t_shutdown pfn_shutdown;
	t_htons pfn_htons;
	t_inet_addr pfn_inet_addr;
	t_setsockopt pfn_setsockopt;
	t_bind pfn_bind;
	t_listen pfn_listen;
#endif

	LPFN_ACCEPTEX lpfnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;
	LPFN_CONNECTEX lpfnConnectEx;
	LPFN_DISCONNECTEX lpfnDisconnectEx;

	CRITICAL_SECTION udp_cs0;
	CRITICAL_SECTION udp_cs1;
	CRITICAL_SECTION udp_cs2;

	CRITICAL_SECTION cs0;
	CRITICAL_SECTION cs1;
	CRITICAL_SECTION cs2;
	CRITICAL_SECTION cs3;
	CRITICAL_SECTION cs4;
	CRITICAL_SECTION cs5;
	CRITICAL_SECTION cs6;	// 没有使用
	CRITICAL_SECTION cs7;
	LONG temp0;
	LONG temp1;

	LONG stemp0;
	LONG stemp1;
}XYTRANSPORT, *PXYTRANSPORT;
//---------------------------------------------------------------------------
#ifdef XYDYNAMIC_LOAD
BOOL XYTransportStartup(PXYTRANSPORT pt, LPVOID parameter, UINT pagesize, UINT contextsize, UINT count, XYTRANSPORT_PROCEDURE procedure, HMODULE hwinsock);
#else
BOOL XYTransportStartup(PXYTRANSPORT pt, LPVOID parameter, UINT pagesize, UINT contextsize, UINT count, XYTRANSPORT_PROCEDURE procedure);
#endif
VOID XYTransportStop(PXYTRANSPORT pt);
VOID XYTransportCleanup(PXYTRANSPORT pt);

// UDP
LPVOID XYUDPBind(PXYTRANSPORT pt, LPVOID customdata, PSOCKADDR_IN psai, USHORT port);
// 此函数只能支持UDP
LPVOID XYUDPBind(PXYTRANSPORT pt, LPVOID customdata);
BOOL XYUDPSendTo(PXYTRANSPORT pt, LPVOID overlapped, LPVOID context, PSOCKADDR_IN psai, const char *buffer, int length);
BOOL XYUDPSendTo(PXYTRANSPORT pt, LPVOID overlapped, LPVOID context, const char *host, int port, const char *buffer, int length);
VOID XYUDPClose(PXYTRANSPORT pt, LPVOID context);

PSOCKADDR XYUDPGetOverlappedAddress(LPVOID overlapped);

VOID XYUDPReleaseOverlapped(PXYTRANSPORT pt, LPVOID overlapped);
VOID XYUDPReleaseContext(PXYTRANSPORT pt, LPVOID context);

LPVOID XYUDPRequestOverlapped(PXYTRANSPORT pt);

// TCP
BOOL XYTCPConnect(PXYTRANSPORT pt, LPVOID customdata, const CHAR *host, USHORT port, PVOID lpsendbuffer, DWORD senddatalength);
LPVOID XYTCPListen(PXYTRANSPORT pt, LPVOID customdata, const CHAR *host, USHORT port, UINT number);

BOOL XYTCPSend(PXYTRANSPORT pt, LPVOID context, const char *buffer, int length, int segmentlength);
BOOL XYTCPDisconnect(PXYTRANSPORT pt, LPVOID context);

UINT XYListenerStop(PXYTRANSPORT pt, LPVOID listener);
UINT XYListenersStop(PXYTRANSPORT pt);

VOID XYTCPReleaseOverlapped(PXYTRANSPORT pt, LPVOID overlapped);
VOID XYTCPReleaseContext(PXYTRANSPORT pt, LPVOID context);

VOID XYOverlappedEnqueue(LPVOID *head, LPVOID *rear, LPVOID *overlappeds);
LPVOID XYOverlappedDequeue(LPVOID *head, LPVOID *rear);

LPVOID XYGetOverlappedContext(LPVOID overlapped);
const char *XYGetOverlappedBuffer(LPVOID overlapped, int *length);

VOID XYSetOverlappedType(LPVOID overlapped, UINT type);
UINT XYGetOverlappedType(LPVOID overlapped);

// 只能在CONNECT或者CLOSE的时候使用
LPVOID XYGetOverlappedCustomData(LPVOID overlapped);

UINT XYTCPGetStackCount(PXYTRANSPORT pt);
//---------------------------------------------------------------------------
#endif
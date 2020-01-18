#include "XYTransport.h"
//---------------------------------------------------------------------------
// 发送投递那里，系统不是分段触发的，否则现在的代码还需要投递剩下的部分到IOCP
// 
//---------------------------------------------------------------------------
#ifndef VC6_COMPILER
//#define VC6_COMPILER
#endif
//---------------------------------------------------------------------------
#ifdef VC6_COMPILER
#define IOC_VENDOR 0x18000000   
#define _WSAIOW(x,y) (IOC_IN|(x)|(y))   
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
#endif
//---------------------------------------------------------------------------
#ifndef XYELIMINATE
//#define XYELIMINATE
#endif
//---------------------------------------------------------------------------
#ifdef XYDYNAMIC_LOAD
#define fn_WSAGetLastError pt->pfn_WSAGetLastError
#define fn_WSAIoctl pt->pfn_WSAIoctl
#define fn_WSASendTo pt->pfn_WSASendTo
#define fn_WSARecvFrom pt->pfn_WSARecvFrom
#define fn_WSASend pt->pfn_WSASend
#define fn_WSARecv pt->pfn_WSARecv
#define fn_WSASocket pt->pfn_WSASocket
#define fn_closesocket pt->pfn_closesocket
#define fn_shutdown pt->pfn_shutdown
#define fn_htons pt->pfn_htons
#define fn_inet_addr pt->pfn_inet_addr
#define fn_setsockopt pt->pfn_setsockopt
#define fn_bind pt->pfn_bind
#define fn_listen pt->pfn_listen
#else
#define fn_WSAGetLastError WSAGetLastError
#define fn_WSAIoctl WSAIoctl
#define fn_WSASendTo WSASendTo
#define fn_WSARecvFrom WSARecvFrom
#define fn_WSASend WSASend
#define fn_WSARecv WSARecv
#define fn_WSASocket WSASocket
#define fn_closesocket closesocket
#define fn_shutdown shutdown
#define fn_htons htons
#define fn_inet_addr inet_addr
#define fn_setsockopt setsockopt
#define fn_bind bind
#define fn_listen listen
#endif
//---------------------------------------------------------------------------
#define XYTCP_LIST_LISTENER													0
#define XYTCP_LIST_CLIENT0													1
#define XYTCP_LIST_CLIENT1													2
//---------------------------------------------------------------------------
#define XYTRANSPORT_TYPE_RANGE_TCP											3

#define XYTRANSPORT_TYPE_UDP_RECV											0
#define XYTRANSPORT_TYPE_UDP_SEND0											1
#define XYTRANSPORT_TYPE_UDP_SEND1											2

#define XYTRANSPORT_TYPE_TCP_OPEN											3
#define XYTRANSPORT_TYPE_TCP_CLOSE											4
#define XYTRANSPORT_TYPE_TCP_RECV											5
#define XYTRANSPORT_TYPE_TCP_SEND											6

// TCP连接结点中的标志
#define XYTRANSPORT_FLAG_TCP_BIRTH											0x01
#define XYTRANSPORT_FLAG_SHUTDOWN0											0x02
#define XYTRANSPORT_FLAG_SHUTDOWN1											0x04
//---------------------------------------------------------------------------
typedef struct tagXYBASE_OVERLAPPED
{
	// The WSAOVERLAPPED structure is compatible with the Windows OVERLAPPED structure.
	//WSAOVERLAPPED ol;
	OVERLAPPED o;

	UINT flags;

	LPVOID next;

	WSABUF wb;

	LPVOID context;

}XYBASE_OVERLAPPED, *PXYBASE_OVERLAPPED;

typedef struct tagXYUDP_OVERLAPPED
{
	XYBASE_OVERLAPPED bo;

	LPBYTE buffer;

	//sockaddr_in6
	SOCKADDR_IN sai;
	int saisize;
}XYUDP_OVERLAPPED, *PXYUDP_OVERLAPPED;

// 可能要修改
typedef struct tagXYUDP_NODE
{
	LPVOID next;
	LPVOID previous;

	LONG posted;

	LPVOID customdata;

	SOCKET s;

	UINT flags;

	CRITICAL_SECTION cs;
}XYUDP_NODE, *PXYUDP_NODE;

typedef struct tagXYTCP_OVERLAPPED
{
	XYBASE_OVERLAPPED bo;

	LPBYTE buffer;

	UINT bufferlength;

}XYTCP_OVERLAPPED, *PXYTCP_OVERLAPPED;

typedef struct tagXYTCP_NODE
{
	LPVOID next;
	LPVOID previous;

	LPVOID head0;

	UINT sending;
	LPVOID head1;
	LPVOID rear1;

	LPVOID customdata;

	LPVOID listener;

	// 转发
	LPVOID transmit;

	SOCKET s;

	UINT flags;

	CRITICAL_SECTION cs1;
}XYTCP_NODE, *PXYTCP_NODE;

typedef struct tagXYLISTENER
{
	LPVOID next;
	LPVOID previous;

	LPVOID head;

	LPVOID customdata;

	SOCKET s;

	CRITICAL_SECTION cs;
}XYLISTENER, *PXYLISTENER;
//---------------------------------------------------------------------------
inline LPVOID XYAlloc(HANDLE heap, UINT size)
{
	return(HeapAlloc(heap, 0, size));
}
inline VOID XYFree(HANDLE heap, LPVOID lpdata)
{
	HeapFree(heap, 0, lpdata);
}

inline LPVOID GetUDPContext(PXYUDP_NODE pun)
{
	return((LPVOID)((LPBYTE)pun + sizeof(XYUDP_NODE)));
}
inline PXYUDP_NODE GetUDPNode(LPVOID context)
{
	return((PXYUDP_NODE)((LPBYTE)context - sizeof(XYUDP_NODE)));
}

inline VOID XYUDPNodeAdd(LPVOID *head, PXYUDP_NODE pun)
{
	pun->previous = NULL;
	pun->next = *head;
	if (pun->next != NULL)
	{
		((PXYUDP_NODE)pun->next)->previous = (LPVOID)pun;
	}
	*head = (LPVOID)pun;
}
inline VOID XYUDPNodeRemove(LPVOID *head, PXYUDP_NODE pun)
{
	if (pun->previous == NULL)
	{
		*head = pun->next;
	}
	else
	{
		((PXYUDP_NODE)pun->previous)->next = pun->next;
	}
	if (pun->next != NULL)
	{
		((PXYUDP_NODE)pun->next)->previous = pun->previous;
	}
}
inline VOID XYUDPNodePush(LPVOID *stack, PXYUDP_NODE pun)
{
	pun->flags = 0;

	pun->next = *stack;
	*stack = (LPVOID)pun;
}
inline PXYUDP_NODE XYUDPNodePop(PXYTRANSPORT pt, LPVOID customdata, SOCKET s, LPVOID *stack)
{
	PXYUDP_NODE pun;
	BOOL flag = FALSE;

	if (*stack == NULL)
	{
		pun = (PXYUDP_NODE)XYAlloc(pt->heap, sizeof(XYUDP_NODE) + pt->contextsize1);

		if (pun != NULL)
		{
			pun->posted = 0;

			pun->flags = 0;

			InitializeCriticalSection(&pun->cs);
		}
	}
	else
	{
		pun = (PXYUDP_NODE)*stack;
		*stack = pun->next;
	}

	if (pun != NULL)
	{
		pun->next = NULL;
		pun->previous = NULL;

		pun->customdata = customdata;

		pun->s = s;
	}

	return(pun);
}

VOID XYUDPNodesStop(PXYTRANSPORT pt, LPVOID head)
{
	PXYUDP_NODE pun;

	pun = (PXYUDP_NODE)head;
	while (pun != NULL)
	{
		//XYTCPDisconnect(pu, pus, FALSE);

		pun = (PXYUDP_NODE)pun->next;
	}
}
// 尚未调用
VOID XYUDPNodesClear(PXYTRANSPORT pt, LPVOID stack)
{
	PXYUDP_NODE pun0, pun1;

	pun0 = (PXYUDP_NODE)stack;
	while (pun0 != NULL)
	{
		pun1 = (PXYUDP_NODE)pun0->next;

		DeleteCriticalSection(&pun0->cs);

		XYFree(pt->heap, (LPVOID)pun0);

		pun0 = pun1;
	}
}
VOID XYUDPNodesPush(PXYTRANSPORT pt, LPVOID *stack, LPVOID *nodes, UINT count)
{
	UINT i, number;

	for (i = 0; i < count; i++)
	{
		nodes[i] = (LPVOID)XYUDPNodePop(pt, NULL, INVALID_SOCKET, stack);
		if (nodes[i] == NULL)
		{
			break;
		}
	}
	number = i;
	for (i = 0; i < number; i++)
	{
		XYUDPNodePush(stack, (PXYUDP_NODE)nodes[i]);
	}
}

inline VOID XYUDPReleaseNode(PXYTRANSPORT pt, PXYUDP_NODE pun)
{
	EnterCriticalSection(&pt->udp_cs1);
	XYUDPNodePush(&pt->stack1, pun);
	LeaveCriticalSection(&pt->udp_cs1);
}

VOID XYUDPTryRecycle(PXYTRANSPORT pt, PXYUDP_NODE pun)
{
	SOCKET s;

	EnterCriticalSection(&pun->cs);
	InterlockedDecrement(&pun->posted);
	if (pun->posted == 0)
	{
		// 此时这个逻辑永远不可能不成立，否则就是想法有问题
		if ((pun->flags&XYTRANSPORT_FLAG_SHUTDOWN0) == 0)
		{
			pun->flags |= XYTRANSPORT_FLAG_SHUTDOWN0;

			// 这个是避免调用者调用关闭连接，而不是仅仅为了删除
			if ((pun->flags&XYTRANSPORT_FLAG_SHUTDOWN1) == 0)
			{
				pun->flags |= XYTRANSPORT_FLAG_SHUTDOWN1;

				EnterCriticalSection(&pt->udp_cs2);
				XYUDPNodeRemove(&pt->udp_head, pun);
				LeaveCriticalSection(&pt->udp_cs2);

				s = pun->s;

				CancelIo((HANDLE)s);
				fn_closesocket(s);
			}

			if (pun != NULL)
			{
				// 通知调用者释放TCP结点
				UINT returns;
				returns = pt->procedure((LPVOID)pt, pt->parameter, GetUDPContext(pun), NULL, NULL, pun->customdata, XYTRANSPORT_UDP_RELEASE, NULL, 0);
				// 调用者释放
				//XYUDPReleaseNode(pt, pl, ptn);
			}
		}
		else
		{
		}
	}
	LeaveCriticalSection(&pun->cs);
}

inline VOID XYUDPOverlappedsPush(PXYTRANSPORT pt, PXYUDP_OVERLAPPED puo0, PXYUDP_OVERLAPPED puo1)
{
	puo1->bo.next = pt->stack0;
	pt->stack0 = (LPVOID)puo0;
}
inline VOID XYUDPOverlappedPush(PXYTRANSPORT pt, PXYUDP_OVERLAPPED puo)
{
	puo->bo.next = pt->stack0;
	pt->stack0 = (LPVOID)puo;
}
inline PXYUDP_OVERLAPPED XYUDPOverlappedPop(PXYTRANSPORT pt)
{
	LPBYTE buffer;
	PXYUDP_OVERLAPPED puo;

	if (pt->stack0 == NULL)
	{
		puo = (PXYUDP_OVERLAPPED)XYAlloc(pt->heap, sizeof(XYUDP_OVERLAPPED));
		if (puo != NULL)
		{
			buffer = (LPBYTE)VirtualAlloc(NULL, pt->bufferlength, MEM_COMMIT, PAGE_READWRITE);
			if (buffer != NULL)
			{
				puo->buffer = buffer;
			}
			else
			{
				XYFree(pt->heap, (LPVOID)puo);
				puo = NULL;
			}
		}
	}
	else
	{
		puo = (PXYUDP_OVERLAPPED)pt->stack0;
		pt->stack0 = puo->bo.next;
	}

	return(puo);
}
VOID XYUDPOverlappedsClear(PXYTRANSPORT pt)
{
	PXYUDP_OVERLAPPED puo0, puo1;

	puo0 = (PXYUDP_OVERLAPPED)pt->stack0;
	pt->stack0 = NULL;
	while (puo0 != NULL)
	{
		puo1 = (PXYUDP_OVERLAPPED)puo0->bo.next;

		VirtualFree((LPVOID)puo0->buffer, 0, MEM_RELEASE);

		XYFree(pt->heap, (LPVOID)puo0);

		puo0 = puo1;
	}
}
VOID XYUDPOverlappedsPush(PXYTRANSPORT pt, LPVOID *overlappeds, UINT count)
{
	UINT i, number;

	for (i = 0; i < count; i++)
	{
		EnterCriticalSection(&pt->udp_cs0);
		overlappeds[i] = (LPVOID)XYUDPOverlappedPop(pt);
		LeaveCriticalSection(&pt->udp_cs0);
		if (overlappeds[i] == NULL)
		{
			break;
		}
	}
	number = i;
	for (i = 0; i < number; i++)
	{
		EnterCriticalSection(&pt->udp_cs0);
		XYUDPOverlappedPush(pt, (PXYUDP_OVERLAPPED)overlappeds[i]);
		LeaveCriticalSection(&pt->udp_cs0);
	}
}

BOOL XYUDPPushReceive(PXYTRANSPORT pt, PXYUDP_OVERLAPPED puo, PXYUDP_NODE pun, SOCKET s)
{
	PXYBASE_OVERLAPPED pbo;
	DWORD flags;
	BOOL result = FALSE;

	if (puo == NULL)
	{
		EnterCriticalSection(&pt->udp_cs0);
		puo = XYUDPOverlappedPop(pt);
		LeaveCriticalSection(&pt->udp_cs0);

		if (puo != NULL)
		{
			puo->bo.context = GetUDPContext(pun);
		}
	}

	if (puo != NULL)
	{
		pbo = &puo->bo;

		ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

		pbo->flags = XYTRANSPORT_TYPE_UDP_RECV;

		pbo->wb.buf = (char *)puo->buffer;
		pbo->wb.len = pt->bufferlength;

		puo->saisize = sizeof(puo->sai);

		flags = 0;

		InterlockedIncrement(&pun->posted);

		result = fn_WSARecvFrom(s, &pbo->wb, 1, NULL, &flags, (PSOCKADDR)&puo->sai, &puo->saisize, &pbo->o, NULL) != SOCKET_ERROR || fn_WSAGetLastError() == WSA_IO_PENDING;
		if (!result)
		{
			InterlockedDecrement(&pun->posted);
		}

		if (!result)
		{
			EnterCriticalSection(&pt->udp_cs0);
			XYUDPOverlappedPush(pt, puo);
			LeaveCriticalSection(&pt->udp_cs0);
		}
	}

	return(result);
}
BOOL XYUDPPushReceives(PXYTRANSPORT pt, UINT *count, PXYUDP_NODE pun, SOCKET s)
{
	UINT i = 0;
	BOOL result;

	do
	{
		result = XYUDPPushReceive(pt, NULL, pun, s);
		if (result)
		{
			i++;
		}
	} while (result && i < pt->count);
	*count = i;
	return(result);
}
VOID XYUDPPopReceive(PXYTRANSPORT pt, PXYUDP_OVERLAPPED puo, PXYUDP_NODE pun, UINT length)
{
	LPVOID overlappeds[1];
	UINT returns;

	if (length > 0)
	{
		puo->bo.wb.len = length;

		puo->bo.next = NULL;

		overlappeds[0] = (LPVOID)puo;

		returns = pt->procedure((LPVOID)pt, pt->parameter, GetUDPContext(pun), overlappeds, NULL, pun->customdata, XYTRANSPORT_UDP_RECV, NULL, 0);
		// 调用者负责回收
	}
	else
	{
		EnterCriticalSection(&pt->udp_cs0);
		XYUDPOverlappedPush(pt, puo);
		LeaveCriticalSection(&pt->udp_cs0);
	}
}
BOOL XYUDPPushSendTo(PXYTRANSPORT pt, PXYUDP_OVERLAPPED puo, PXYUDP_NODE pun, PSOCKADDR_IN psai, const char *buffer, int length)
{
	PXYBASE_OVERLAPPED pbo;
	BOOL result = FALSE;

	if (puo == NULL)
	{
		EnterCriticalSection(&pt->udp_cs0);
		puo = XYUDPOverlappedPop(pt);
		LeaveCriticalSection(&pt->udp_cs0);

		if (puo != NULL)
		{
			puo->bo.context = GetUDPContext(pun);
		}
	}

	if (puo != NULL)
	{
		pbo = &puo->bo;

		ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

		if (psai != NULL)
		{
			CopyMemory(&puo->sai, psai, sizeof(SOCKADDR_IN));
		}

		if (length < 0)
		{
			length = -length;

			pbo->flags = XYTRANSPORT_TYPE_UDP_SEND0;
		}
		else
		{
			pbo->flags = XYTRANSPORT_TYPE_UDP_SEND1;
		}
		if (buffer != NULL)
		{
			pbo->wb.buf = (char *)buffer;
		}
		pbo->wb.len = length;

		EnterCriticalSection(&pun->cs);

		InterlockedIncrement(&pun->posted);

		result = fn_WSASendTo(pun->s, &pbo->wb, 1, NULL, 0, (PSOCKADDR)&puo->sai, sizeof(SOCKADDR_IN), &pbo->o, NULL) != SOCKET_ERROR || fn_WSAGetLastError() == WSA_IO_PENDING;
		if (!result)
		{
			InterlockedDecrement(&pun->posted);
		}

		LeaveCriticalSection(&pun->cs);

		if (!result)
		{
			EnterCriticalSection(&pt->udp_cs0);
			XYUDPOverlappedPush(pt, puo);
			LeaveCriticalSection(&pt->udp_cs0);
		}
	}

	return(result);
}
VOID XYUDPPopSendTo(PXYTRANSPORT pt, PXYUDP_OVERLAPPED puo, PXYUDP_NODE pun, SOCKET s, UINT length)
{
	PXYBASE_OVERLAPPED pbo;
	LPVOID overlapped;
	char *buffer;
	UINT returns;

	pbo = &puo->bo;

	buffer = pbo->wb.buf;
	//length = pbo->wb.len;

	puo->bo.next = NULL;
	overlapped = (LPVOID)puo;
	returns = pt->procedure((LPVOID)pt, pt->parameter, GetUDPContext(pun), &overlapped, NULL, pun->customdata, XYTRANSPORT_UDP_SEND, buffer, length);
	// 调用者负责回收
}

// private
// RAW, UDP
SOCKET XYUDPSocket(PXYTRANSPORT pt, PSOCKADDR_IN psai, BOOL raw)
{
	DWORD iocontorlcode;
	LPVOID lpbuffer;
	DWORD buffer0[10];
	DWORD buffer1 = 1;
	DWORD size0;
	DWORD size1;
	DWORD numberofbytes = 0;
	BOOL flag = TRUE;
	SOCKET s;
	int type;
	int protocol;

	if (psai != NULL)
	{
		type = SOCK_RAW;
		protocol = IPPROTO_IP;
	}
	else
	{
		type = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
	}
	s = fn_WSASocket(AF_INET, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (s != INVALID_SOCKET)
	{
		if (psai != NULL)
		{
			flag = fn_bind(s, (PSOCKADDR)psai, sizeof(SOCKADDR_IN)) != SOCKET_ERROR;

			if (flag)
			{
				iocontorlcode = SIO_RCVALL;

				lpbuffer = (LPVOID)&buffer0;
				size0 = sizeof(buffer0);

				buffer1 = 1;
			}
		}
		else
		{
			iocontorlcode = SIO_UDP_CONNRESET;

			lpbuffer = NULL;
			size0 = 0;

			//下面的函数用于解决远端突然关闭会导致WSARecvFrom返回10054错误导致服务器完成队列中没有reeceive操作而设置
			// bNewBehavior
			buffer1 = FALSE;
		}
		size1 = sizeof(DWORD);
		flag = flag && fn_WSAIoctl(s, iocontorlcode, &buffer1, size1, lpbuffer, size0, &numberofbytes, NULL, NULL) == 0;
		flag = flag && CreateIoCompletionPort((HANDLE)s, pt->hcompletion, (DWORD)s, 0) == pt->hcompletion;
		if (!flag)
		{
			fn_closesocket(s);
			s = INVALID_SOCKET;
		}
	}

	return(s);
}
LPVOID XYUDPBind(PXYTRANSPORT pt, LPVOID customdata, PSOCKADDR_IN psai, USHORT port)
{
	PXYUDP_NODE pun;
	LPVOID result = NULL;
	SOCKADDR_IN sai;
	SOCKET s;
	UINT count = 0;
	BOOL flag = FALSE;

	s = XYUDPSocket(pt, psai, port == 0);
	if (s != INVALID_SOCKET)
	{
		EnterCriticalSection(&pt->udp_cs1);
		pun = XYUDPNodePop(pt, customdata, s, &pt->stack1);
		LeaveCriticalSection(&pt->udp_cs1);

		if (pun != NULL)
		{
			flag = psai != NULL;
			if (!flag)
			{
				ZeroMemory(&sai, sizeof(SOCKADDR_IN));
				sai.sin_family = AF_INET;
				sai.sin_addr.S_un.S_addr = INADDR_ANY;
				sai.sin_port = fn_htons(port);
				flag = fn_bind(s, (PSOCKADDR)&sai, sizeof(sai)) != SOCKET_ERROR;
			}
			if (flag)
			{
				EnterCriticalSection(&pt->udp_cs2);
				XYUDPNodeAdd(&pt->udp_head, pun);
				LeaveCriticalSection(&pt->udp_cs2);

				flag = XYUDPPushReceives(pt, &count, pun, s);

				if (flag)
				{
					result = GetUDPContext(pun);
				}
				else
				{
					if (count == 0)
					{
						EnterCriticalSection(&pt->udp_cs2);
						XYUDPNodeRemove(&pt->head, pun);
						LeaveCriticalSection(&pt->udp_cs2);
					}
				}
			}

			if (!flag)
			{
				if (count == 0)
				{
					EnterCriticalSection(&pt->udp_cs1);
					XYUDPNodePush(&pt->stack1, pun);
					LeaveCriticalSection(&pt->udp_cs1);
				}
			}
		}

		if (!flag)
		{
			if (count == 0)
			{
				CancelIo((HANDLE)s);

				fn_closesocket(s);
			}
		}
	}

	return(result);
}
// 此函数只能支持UDP
LPVOID XYUDPBind(PXYTRANSPORT pt, LPVOID customdata)
{
	PXYUDP_NODE pun;
	LPVOID result = NULL;
	SOCKET s;

	s = XYUDPSocket(pt, NULL, FALSE);
	if (s != INVALID_SOCKET)
	{
		EnterCriticalSection(&pt->udp_cs1);
		pun = XYUDPNodePop(pt, customdata, s, &pt->stack1);
		LeaveCriticalSection(&pt->udp_cs1);

		if (pun != NULL)
		{
			EnterCriticalSection(&pt->udp_cs2);
			XYUDPNodeAdd(&pt->udp_head, pun);
			LeaveCriticalSection(&pt->udp_cs2);

			// 此时不能投递接收

			result = GetUDPContext(pun);
		}
		else
		{
			CancelIo((HANDLE)s);

			fn_closesocket(s);
		}
	}

	return(result);
}
BOOL XYUDPSendTo(PXYTRANSPORT pt, LPVOID overlapped, LPVOID context, PSOCKADDR_IN psai, const char *buffer, int length)
{
	PXYUDP_NODE pun;
	BOOL flag;
	BOOL result = FALSE;

	pun = GetUDPNode(context);

	EnterCriticalSection(&pun->cs);
	if ((pun->flags&XYTRANSPORT_FLAG_SHUTDOWN1) == 0 && (pun->flags&XYTRANSPORT_FLAG_SHUTDOWN0) == 0)
	{
		flag = pun->posted == 0;
		if (flag)
		{
			length = -length;
		}

		if (!XYUDPPushSendTo(pt, (PXYUDP_OVERLAPPED)overlapped, pun, psai, buffer, length))
		{
			if (flag)
			{
				EnterCriticalSection(&pt->udp_cs2);
				XYUDPNodeRemove(&pt->udp_head, pun);
				LeaveCriticalSection(&pt->udp_cs2);

				EnterCriticalSection(&pt->udp_cs1);
				XYUDPNodePush(&pt->stack1, pun);
				LeaveCriticalSection(&pt->udp_cs1);
			}
		}
	}
	LeaveCriticalSection(&pun->cs);

	return(result);
}
BOOL XYUDPSendTo(PXYTRANSPORT pt, LPVOID overlapped, LPVOID context, const char *host, int port, const char *buffer, int length)
{
	SOCKADDR_IN sai;

	ZeroMemory(&sai, sizeof(SOCKADDR_IN));
	sai.sin_family = AF_INET;
	sai.sin_addr.S_un.S_addr = fn_inet_addr(host);
	sai.sin_port = fn_htons(port);

	return(XYUDPSendTo(pt, overlapped, context, &sai, buffer, length));
}
VOID XYUDPClose(PXYTRANSPORT pt, LPVOID context)
{
	PXYUDP_NODE pun;
	SOCKET s;

	pun = GetUDPNode(context);

	EnterCriticalSection(&pun->cs);
	if ((pun->flags&XYTRANSPORT_FLAG_SHUTDOWN1) == 0 && (pun->flags&XYTRANSPORT_FLAG_SHUTDOWN0) == 0)
	{
		pun->flags |= XYTRANSPORT_FLAG_SHUTDOWN1;

		s = pun->s;

		CancelIo((HANDLE)s);
		fn_closesocket(s);
	}
	LeaveCriticalSection(&pun->cs);
}

PSOCKADDR XYUDPGetOverlappedAddress(LPVOID overlapped)
{
	PXYUDP_OVERLAPPED puo = (PXYUDP_OVERLAPPED)overlapped;

	return((PSOCKADDR)&puo->sai);
}

VOID XYUDPReleaseOverlapped(PXYTRANSPORT pt, LPVOID overlapped)
{
	EnterCriticalSection(&pt->udp_cs0);
	XYUDPOverlappedPush(pt, (PXYUDP_OVERLAPPED)overlapped);
	LeaveCriticalSection(&pt->udp_cs0);
}
VOID XYUDPReleaseContext(PXYTRANSPORT pt, LPVOID context)
{
	PXYUDP_NODE pun;

	pun = GetUDPNode(context);

	XYUDPReleaseNode(pt, pun);
}

LPVOID XYUDPRequestOverlapped(PXYTRANSPORT pt)
{
	PXYUDP_OVERLAPPED puo;

	EnterCriticalSection(&pt->udp_cs0);
	puo = XYUDPOverlappedPop(pt);
	LeaveCriticalSection(&pt->udp_cs0);

	if (puo != NULL)
	{
		puo->bo.wb.buf = (char *)puo->buffer;
	}

	return((LPVOID)puo);
}
//

inline LPVOID GetTCPContext(PXYTCP_NODE ptn)
{
	return((LPVOID)((LPBYTE)ptn + sizeof(XYTCP_NODE)));
}
inline PXYTCP_NODE GetTCPNode(LPVOID context)
{
	return((PXYTCP_NODE)((LPBYTE)context - sizeof(XYTCP_NODE)));
}

inline VOID XYTCPOverlappedsPush(PXYTRANSPORT pt, PXYTCP_OVERLAPPED pto0, PXYTCP_OVERLAPPED pto1)
{
	pto1->bo.next=pt->stack;
	pt->stack = (LPVOID)pto0;

	//
	UINT count=1;
	InterlockedDecrement(&pt->stemp0);
	while (pto0 != pto1)
	{
		count++;

		InterlockedDecrement(&pt->stemp0);
		pto0 = (PXYTCP_OVERLAPPED)pto0->bo.next;
	}
	//
}
inline VOID XYTCPOverlappedPush(PXYTRANSPORT pt, PXYTCP_OVERLAPPED pto)
{
	pto->bo.next = pt->stack;
	pt->stack = (LPVOID)pto;

	InterlockedDecrement(&pt->stemp0);

#ifdef XYDEBUG
	//OutputDebugValue(_T("pto入队"), pto->sequence);
#endif
}
inline PXYTCP_OVERLAPPED XYTCPOverlappedPop(PXYTRANSPORT pt)
{
	LPBYTE buffer;
	PXYTCP_OVERLAPPED pto = NULL;

	if (pt->stack == NULL)
	{
		InterlockedIncrement(&pt->stemp1);

		buffer = (LPBYTE)VirtualAlloc(NULL, pt->bufferlength, MEM_COMMIT, PAGE_READWRITE);
		if (buffer != NULL)
		{
			pto = (PXYTCP_OVERLAPPED)XYAlloc(pt->heap, sizeof(XYTCP_OVERLAPPED));
			if (pto != NULL)
			{
				pto->buffer = buffer;
			}
			else
			{
				VirtualFree((LPVOID)buffer, 0, MEM_RELEASE);
			}
		}
	}
	else
	{
		pto = (PXYTCP_OVERLAPPED)pt->stack;
		pt->stack = pto->bo.next;
	}

	if (pto != NULL)
	{
		//
	}

	InterlockedIncrement(&pt->stemp0);

	return(pto);
}
VOID XYTCPOverlappedsClear(PXYTRANSPORT pt)
{
	PXYTCP_OVERLAPPED pto0, pto1;

	pto0 = (PXYTCP_OVERLAPPED)pt->stack;
	pt->stack = NULL;
	while (pto0 != NULL)
	{
		pto1 = (PXYTCP_OVERLAPPED)pto0->bo.next;

		//if (pto0->buffer != NULL)
		{
			VirtualFree((LPVOID)pto0->buffer, 0, MEM_RELEASE);
		}
		XYFree(pt->heap, (LPVOID)pto0);

		pto0 = pto1;
	}
}
VOID XYTCPOverlappedsPush(PXYTRANSPORT pt, LPVOID *overlappeds, UINT count)
{
	UINT i, number;

	for (i = 0; i < count; i++)
	{
		overlappeds[i] = (LPVOID)XYTCPOverlappedPop(pt);
		if (overlappeds[i] == NULL)
		{
			break;
		}
	}
	number = i;
	for (i = 0; i < number; i++)
	{
		XYTCPOverlappedPush(pt, (PXYTCP_OVERLAPPED)overlappeds[i]);
	}
}

inline VOID XYTCPOverlappedsAdd(PXYTCP_NODE ptn, PXYTCP_OVERLAPPED pto0, PXYTCP_OVERLAPPED pto1)
{
	if (ptn->rear1 == NULL)
	{
		ptn->head1 = (LPVOID)pto0;
	}
	else
	{
		((PXYTCP_OVERLAPPED)ptn->rear1)->bo.next = (LPVOID)pto0;
	}
	ptn->rear1 = (LPVOID)pto1;
}
inline PXYTCP_OVERLAPPED XYTCPOverlappedsRemove(PXYTCP_NODE ptn, PXYTCP_OVERLAPPED *pto)
{
	PXYTCP_OVERLAPPED result;

	result = (PXYTCP_OVERLAPPED)ptn->rear1;
	if (result != NULL)
	{
		*pto = (PXYTCP_OVERLAPPED)ptn->head1;

		ptn->head1 = ptn->rear1 = NULL;
	}

	return(result);
}
PXYTCP_OVERLAPPED XYTCPRemoveSent(PXYTCP_NODE ptn, PXYTCP_OVERLAPPED pto, UINT length)
{
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED result;
	UINT l;

	result = pto;

	pbo = &pto->bo;

	l = length < pbo->wb.len ? length : pbo->wb.len;

	pbo->wb.buf += l;
	pbo->wb.len -= l;
	if (pbo->wb.len == 0)
	{
		result = (PXYTCP_OVERLAPPED)ptn->head1;

		if (result != NULL)
		{
			ptn->head1 = result->bo.next;
			if (ptn->head1 == NULL)
			{
				ptn->rear1 = ptn->head1;
			}
		}
	}

	return(result);
}

inline VOID XYTCPNodeAdd(LPVOID *head, PXYTCP_NODE ptn)
{
	ptn->previous = NULL;
	ptn->next = *head;
	if (ptn->next != NULL)
	{
		((PXYTCP_NODE)ptn->next)->previous = (LPVOID)ptn;
	}
	*head=(LPVOID)ptn;
}
inline VOID XYTCPNodeRemove(LPVOID *head, PXYTCP_NODE ptn)
{
	if (ptn->previous == NULL)
	{
		*head = ptn->next;
	}
	else
	{
		((PXYTCP_NODE)ptn->previous)->next = ptn->next;
	}
	if(ptn->next!=NULL)
	{
		((PXYTCP_NODE)ptn->next)->previous = ptn->previous;
	}
}
inline VOID XYTCPNodePush(LPVOID *stack, PXYTCP_NODE ptn)
{
	// 回收的时候清空别的标志
	ptn->flags = 0;

	ptn->next=*stack;
	*stack=(LPVOID)ptn;
}
inline PXYTCP_NODE XYTCPNodePop(PXYTRANSPORT pt, LPVOID listener, LPVOID customdata, LPVOID *stack)
{
	PXYTCP_NODE ptn;
	SOCKADDR_IN name;
	BOOL flag = FALSE;

	InterlockedIncrement(&pt->temp0);

	if (*stack == NULL)
	{
		InterlockedIncrement(&pt->temp1);

		ptn = (PXYTCP_NODE)XYAlloc(pt->heap, sizeof(XYTCP_NODE) + pt->contextsize);
		if (ptn != NULL)
		{
			ptn->s = fn_WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			if (ptn->s != INVALID_SOCKET)
			{
				/*
				tcp_keepalive inKeepAlive = { 0 };
				unsigned long ulInLen = sizeof(tcp_keepalive);
				tcp_keepalive outKeepAlive = { 0 };
				unsigned long ulOutLen = sizeof(tcp_keepalive);
				unsigned long ulBytesReturn = 0;

				int ret;

				inKeepAlive.onoff = 1;
				inKeepAlive.keepaliveinterval = 5000; //单位为毫秒
				inKeepAlive.keepalivetime = 1000;     //单位为毫秒

				ret = fn_WSAIoctl(pc->s, SIO_KEEPALIVE_VALS, (LPVOID)&inKeepAlive, ulInLen, (LPVOID)&outKeepAlive, ulOutLen, &ulBytesReturn, NULL, NULL);
				*/
				//int optval = 5000;
				//fn_setsockopt(ptn->s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&optval, sizeof(int));

				//
#ifdef XYTCP_NODELAY
				int optval = 1;
				fn_setsockopt(ptn->s, IPPROTO_TCP, TCP_NODELAY, (const char *)&optval, sizeof(int));
#endif
				//
				//int optval = 0;
				//fn_setsockopt(ptn->s, SOL_SOCKET, SO_SNDBUF, (const char*)&optval, sizeof(int));

				flag = TRUE;

				if (listener == NULL)
				{
					flag = FALSE;

					ZeroMemory(&name, sizeof(name));
					name.sin_family = AF_INET;
					name.sin_addr.S_un.S_addr = INADDR_ANY;
					//name.sin_port = fn_htons(0);
					name.sin_port = 0;
					if (fn_bind(ptn->s, (const SOCKADDR *)&name, sizeof(name)) == 0)
					{
						flag = TRUE;
					}
				}

				if (flag)
				{
					flag = CreateIoCompletionPort((HANDLE)ptn->s, pt->hcompletion, (ULONG_PTR)ptn->s, 0) == pt->hcompletion;
				}

				if (flag)
				{
					ptn->flags = XYTRANSPORT_FLAG_TCP_BIRTH;

					ptn->sending = 0;
					ptn->head1 = NULL;
					ptn->rear1 = NULL;

					InitializeCriticalSection(&ptn->cs1);
				}
				else
				{
					fn_closesocket(ptn->s);
				}
			}
			
			if (!flag)
			{
				XYFree(pt->heap, (LPVOID)ptn);
				ptn = NULL;
			}
		}
	}
	else
	{
		ptn = (PXYTCP_NODE)*stack;
		*stack=ptn->next;
	}

	if (ptn != NULL)
	{
		ptn->next = NULL;
		ptn->previous = NULL;

		ptn->listener = listener;

		// 避免发送异常的时候提前回收了ptn
		ptn->head0 = (LPVOID)-1;

		ptn->customdata = customdata;
	}

	return(ptn);
}
VOID XYTCPNodesClear(PXYTRANSPORT pt, LPVOID *stack)
{
	PXYTCP_NODE ptn0, ptn1;

	ptn0 = (PXYTCP_NODE)*stack;
	*stack = NULL;
	while (ptn0 != NULL)
	{
		ptn1 = (PXYTCP_NODE)ptn0->next;

		CancelIo((HANDLE)ptn0->s);
		fn_closesocket(ptn0->s);

		DeleteCriticalSection(&ptn0->cs1);

		XYFree(pt->heap, (LPVOID)ptn0);

		ptn0 = ptn1;
	}
}

inline VOID XYTCPReleaseNode(PXYTRANSPORT pt, PXYLISTENER pl, PXYTCP_NODE ptn)
{
	CRITICAL_SECTION *pcs1;
	LPVOID *stack;

	if (pl != NULL)
	{
		pcs1 = &pt->cs2;
		stack = &pt->stacks[XYTCP_LIST_CLIENT1];
	}
	else
	{
		pcs1 = &pt->cs1;
		stack = &pt->stacks[XYTCP_LIST_CLIENT0];
	}

	EnterCriticalSection(pcs1);
	XYTCPNodePush(stack, ptn);
	LeaveCriticalSection(pcs1);

	InterlockedDecrement(&pt->temp0);
}

BOOL XYTCPDisconnect(PXYTRANSPORT pt, PXYLISTENER pl, PXYTCP_NODE ptn, SOCKET s, UINT type)
{
	GUID id = WSAID_DISCONNECTEX;
	DWORD numberofbytes = 0;
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED pto = NULL;
	CRITICAL_SECTION *pcs0;
	LPVOID *head;
	UINT returns;
	BOOL result = FALSE;

	if (pt->lpfnDisconnectEx != NULL || fn_WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &pt->lpfnDisconnectEx, sizeof(pt->lpfnDisconnectEx), &numberofbytes, NULL, NULL) != SOCKET_ERROR)
	{
		if (pl != NULL)
		{
			pcs0 = &pl->cs;
			head = &pl->head;
		}
		else
		{
			pcs0 = &pt->cs0;
			head = &pt->heads[XYTCP_LIST_CLIENT0];
		}

		EnterCriticalSection(&ptn->cs1);
		switch (type)
		{
		case XYTRANSPORT_TYPE_TCP_RECV:
			ptn->head0 = NULL;
			break;
		case XYTRANSPORT_TYPE_TCP_SEND:
			ptn->sending--;
			break;
		default:
			break;
		}
		if ((ptn->flags&XYTRANSPORT_FLAG_SHUTDOWN0) != 0)
		{
			// 此逻辑如果成功，则代码设计有问题
#ifdef FIND_TROJAN_BUG
			OutputDebugValue(_T("SHUTDOWN 1"), (int)ptn);
			//MessageBox(NULL, _T("SHUTDOWN 1"), _T("Error"), MB_OK);
#endif
		}
		ptn->flags |= XYTRANSPORT_FLAG_SHUTDOWN0;
		if (ptn->sending == 0 && ptn->head0 == NULL)
		{
			if ((ptn->flags&XYTRANSPORT_FLAG_SHUTDOWN1) == 0)
			{
				ptn->flags |= XYTRANSPORT_FLAG_SHUTDOWN1;

				EnterCriticalSection(pcs0);
				XYTCPNodeRemove(head, ptn);
				LeaveCriticalSection(pcs0);
			}
			//

			EnterCriticalSection(&pt->cs4);
			pto = XYTCPOverlappedPop(pt);
			LeaveCriticalSection(&pt->cs4);

			if (pto != NULL)
			{
				// (*)

				pbo = &pto->bo;

				ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

				pbo->context = GetTCPContext(ptn);

				pbo->flags = XYTRANSPORT_TYPE_TCP_CLOSE;

				result = pt->lpfnDisconnectEx(s, &pbo->o, TF_REUSE_SOCKET, 0);

				if (!result)
				{
					int error = fn_WSAGetLastError();
					result = error == ERROR_IO_PENDING;

					if (!result)
					{
#ifdef XYDEBUG
						OutputDebugError(_T("DisconnectEx"), error, (int)s, 0);
#endif
					}
				}

				if (result)
				{
					pto = NULL;
					//ptn = NULL;
				}
				else
				{
					/*
					DeleteCriticalSection(&ptn->cs);
					DeleteCriticalSection(&ptn->cs0);
					DeleteCriticalSection(&ptn->cs1);
					XYFree(pt->heap, (LPVOID)ptn);
					*/

					pbo->next = NULL;
					LPVOID overlapped = (LPVOID)pto;
					returns = pt->procedure((LPVOID)pt, pt->parameter, GetTCPContext(ptn), &overlapped, ptn->listener, ptn->customdata, XYTRANSPORT_TCP_CLOSE, NULL, XYTRANSPORT_ERROR_ABORT1);

					pto = NULL;

					//ptn = NULL;
				}
			}
#ifdef FIND_TROJAN_BUG
			else
			{
				OutputDebugString(_T("申请Overlapped失败 DisconnectEx"));
			}
#endif
		}
		else
		{
			result = TRUE;
#ifdef FIND_TROJAN_BUG
			// 到达这里很正常
			OutputDebugValue(_T("Error ptn-------------------"), (int)ptn->head0);
#endif
			//ptn = NULL;
		}
		LeaveCriticalSection(&ptn->cs1);

		if (pto != NULL)
		{
			EnterCriticalSection(&pt->cs4);
			XYTCPOverlappedPush(pt, pto);
			LeaveCriticalSection(&pt->cs4);
		}
	}
	
	return(result);
}

UINT XYTCPNodesStop(PXYTRANSPORT pt, PXYLISTENER pl, LPVOID *head, CRITICAL_SECTION *pcs)
{
	PXYTCP_NODE ptn;
	UINT result = 0;

	EnterCriticalSection(pcs);
	ptn = (PXYTCP_NODE)*head;
	while (ptn != NULL)
	{
		XYTCPDisconnect(pt, GetTCPContext(ptn));

		LeaveCriticalSection(pcs);

		result++;

		EnterCriticalSection(pcs);
		ptn = (PXYTCP_NODE)*head;
	}
	LeaveCriticalSection(pcs);

	return(result);
}
VOID XYTCPNodesPush(PXYTRANSPORT pt, LPVOID listener, LPVOID *stack, LPVOID *nodes, UINT count)
{
	UINT i, number;

	for (i = 0; i < count; i++)
	{
		nodes[i] = (LPVOID)XYTCPNodePop(pt, listener, NULL, stack);
		if (nodes[i] == NULL)
		{
			break;
		}
	}
	number = i;
	for (i = 0; i < number; i++)
	{
		XYTCPNodePush(stack, (PXYTCP_NODE)nodes[i]);

		InterlockedDecrement(&pt->temp0);
	}
}

inline VOID XYListenerAdd(LPVOID *head, PXYLISTENER pl)
{
	pl->previous = NULL;
	pl->next = *head;
	if (pl->next != NULL)
	{
		((PXYLISTENER)pl->next)->previous = (LPVOID)pl;
	}
	*head = (LPVOID)pl;
}
VOID XYListenerRemove(LPVOID *head, PXYLISTENER pl)
{
	if (pl->previous == NULL)
	{
		*head = pl->next;
	}
	else
	{
		((PXYLISTENER)pl->previous)->next = pl->next;
	}
	if (pl->next != NULL)
	{
		((PXYLISTENER)pl->next)->previous = pl->previous;
	}
}
inline VOID XYListenerPush(LPVOID *stack, PXYLISTENER pl)
{
	pl->next = *stack;
	*stack = (LPVOID)pl;
}
inline PXYLISTENER XYListenerPop(PXYTRANSPORT pt, LPVOID customdata, LPVOID *stack)
{
	PXYLISTENER pl;

	if (*stack == NULL)
	{
		pl = (PXYLISTENER)XYAlloc(pt->heap, sizeof(XYLISTENER));
		if (pl != NULL)
		{
			pl->s = INVALID_SOCKET;

			InitializeCriticalSection(&pl->cs);
		}
	}
	else
	{
		pl = (PXYLISTENER)*stack;
		*stack = pl->next;
	}

	if (pl != NULL)
	{
		pl->next = NULL;
		pl->previous = NULL;

		pl->head = NULL;

		pl->customdata = customdata;
	}

	return(pl);
}
UINT XYListenerStop(PXYTRANSPORT pt, PXYLISTENER pl)
{
	UINT result;

#ifdef XYFIND_BUG
	OutputDebugValue(_T("Stop 0"), (int)pl->s);
#endif

	// 临界感觉不需要加到这里
	EnterCriticalSection(&pt->cs7);
	result = XYTCPNodesStop(pt, pl, &pl->head, &pl->cs);

	//EnterCriticalSection(&pt->cs7);
	XYListenerRemove(&pt->heads[XYTCP_LIST_LISTENER], pl);
	LeaveCriticalSection(&pt->cs7);

#ifdef XYFIND_BUG
	OutputDebugValue(_T("Stop 1"), (int)pl->s);
#endif

	//
	CancelIo((HANDLE)pl->s);
	fn_closesocket(pl->s);
	//

#ifdef XYFIND_BUG
	OutputDebugValue(_T("Stop 2"), (int)pl->s);
#endif

	EnterCriticalSection(&pt->cs3);
	XYListenerPush(&pt->stacks[XYTCP_LIST_LISTENER], pl);
	LeaveCriticalSection(&pt->cs3);

#ifdef XYFIND_BUG
	OutputDebugValue(_T("Stop 3"), (int)pl->s);
#endif

	return(result);
}
UINT XYListenersStop(PXYTRANSPORT pt, LPVOID *head, CRITICAL_SECTION *pcs)
{
	PXYLISTENER pl;
	UINT result = 0;

	EnterCriticalSection(pcs);
	pl = (PXYLISTENER)*head;
	while (pl != NULL)
	{
		XYListenerStop(pt, pl);
		LeaveCriticalSection(pcs);

		result++;

		EnterCriticalSection(pcs);
		pl = (PXYLISTENER)*head;
	}
	LeaveCriticalSection(pcs);
	return(result);
}
VOID XYListenersClear(PXYTRANSPORT pt, LPVOID stack)
{
	PXYLISTENER pl0, pl1;

	pl0 = (PXYLISTENER)stack;
	while (pl0 != NULL)
	{
		pl1 = (PXYLISTENER)pl0->next;

		DeleteCriticalSection(&pl0->cs);

		XYFree(pt->heap, (LPVOID)pl0);

		pl0 = pl1;
	}
}
VOID XYListenersPush(PXYTRANSPORT pt, LPVOID *stack, LPVOID *listeners, UINT count)
{
	UINT i, number;

	for (i = 0; i < count; i++)
	{
		listeners[i] = (LPVOID)XYListenerPop(pt, NULL, stack);
		if (listeners[i] == NULL)
		{
			break;
		}
	}
	number = i;
	for (i = 0; i < number; i++)
	{
		XYListenerPush(stack, (PXYLISTENER)listeners[i]);
	}
}

BOOL XYTCPPushReceive(PXYTRANSPORT pt, PXYTCP_NODE ptn, SOCKET s)
{
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED pto;
	DWORD numberofbytes;
	DWORD flags = 0;
	BOOL result = FALSE;
#ifdef XYDEBUG
	int error;
#endif

	EnterCriticalSection(&pt->cs4);
	pto = XYTCPOverlappedPop(pt);
	LeaveCriticalSection(&pt->cs4);

	if (pto != NULL)
	{
		pbo = &pto->bo;

		ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

		pbo->context = GetTCPContext(ptn);

		pbo->flags = XYTRANSPORT_TYPE_TCP_RECV;

		pbo->wb.buf = (char *)pto->buffer;
		pbo->wb.len = pt->bufferlength;

#ifdef XYDEBUG
		result = fn_WSARecv(s, &pbo->wb, 1, &numberofbytes, &flags, &pbo->o, NULL) != SOCKET_ERROR;
		if (!result)
		{
			error = fn_WSAGetLastError();
			result = error == WSA_IO_PENDING;
			if (!result)
			{
				OutputDebugString(_T("WSARecv Error"));
				//OutputDebugError(_T("WSARecv"), error);
			}
		}
#else
		result = fn_WSARecv(s, &pbo->wb, 1, &numberofbytes, &flags, &pbo->o, NULL) != SOCKET_ERROR || fn_WSAGetLastError() == WSA_IO_PENDING;
#endif

		if (!result)
		{
			EnterCriticalSection(&pt->cs4);
			XYTCPOverlappedPush(pt, pto);
			LeaveCriticalSection(&pt->cs4);
		}
	}
	else
	{
#ifdef FIND_TROJAN_BUG
		OutputDebugString(_T("申请Node失败 PushReceive"));
#endif
	}

	return(result);
}

VOID XYTCPPopReceive(PXYTRANSPORT pt, PXYTCP_OVERLAPPED pto, PXYTCP_NODE ptn, UINT length)
{
	LPVOID overlappeds[1];
	UINT returns;
	BOOL result;

	pto->bo.wb.len = length;

	if (length > 0)
	{
		pto->bo.next = NULL;

		overlappeds[0] = (LPVOID)pto;

		returns = pt->procedure((LPVOID)pt, pt->parameter, GetTCPContext(ptn), overlappeds, ptn->listener, ptn->customdata, XYTRANSPORT_TCP_RECV, NULL, 0);
		// 调用者负责回收
	}
	else
	{
		EnterCriticalSection(&pt->cs4);
		XYTCPOverlappedPush(pt, pto);
		LeaveCriticalSection(&pt->cs4);
	}
}

int XYTCPMergeBuffer(PXYTRANSPORT pt, PXYTCP_NODE ptn, const char *buffer, int length)
{
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED pto;
	int result = 0;

	pto = (PXYTCP_OVERLAPPED)ptn->rear1;
	if (pto != NULL)
	{
		pbo = &pto->bo;

		if (pbo->wb.buf == (char *)pto->buffer)
		{
			result = pt->bufferlength - pbo->wb.len;
			result = result < length ? result : length;

			if (result > 0)
			{
				CopyMemory(pbo->wb.buf + pbo->wb.len, buffer, result);
				pbo->wb.len += result;

				pto->bufferlength += result;
			}
		}
	}

	return(result);
}
PXYTCP_OVERLAPPED XYTCPLoadBuffer(PXYTRANSPORT pt, PXYTCP_OVERLAPPED *head, LPVOID context, const char *buffer, int length, int segmentlength)
{
	PXYTCP_OVERLAPPED pto0 = NULL;
	PXYTCP_OVERLAPPED pto1 = NULL;
	PXYTCP_OVERLAPPED pto;
	int offset = 0;
	int l;

	while (offset < length)
	{
		l = length - offset;
		if (segmentlength > 0)
		{
			l = l < segmentlength ? l : segmentlength;
		}
		else
		{
			l = l < pt->bufferlength ? l : pt->bufferlength;
		}

		pto = pto1;
		pto1 = XYTCPOverlappedPop(pt);
		if (pto1 != NULL)
		{
			if (pto == NULL)
			{
				pto0 = pto1;
			}
			else
			{
				pto->bo.next = (LPVOID)pto1;
			}

			if (segmentlength > 0)
			{
				pto1->bo.wb.buf = (char *)(buffer + offset);
			}
			else
			{
				pto1->bo.wb.buf = (char *)pto1->buffer;
				CopyMemory(pto1->bo.wb.buf, buffer + offset, l);
			}
			pto1->bo.wb.len = l;

			//
			pto1->bufferlength = l;
			//

			pto1->bo.context = context;

			pto1->bo.flags = XYTRANSPORT_TYPE_TCP_SEND;
		}
		else
		{
			if (pto0 != NULL)
			{
				pto->bo.next = NULL;

				while (pto0 != NULL)
				{
					pto = (PXYTCP_OVERLAPPED)pto0->bo.next;

					XYTCPOverlappedPush(pt, pto0);

					pto0 = pto;
				}
			}

			break;
		}

		offset += l;
	}

	if (pto1 != NULL)
	{
		pto1->bo.next = NULL;

		*head = pto0;
	}

	return(pto1);
}
inline BOOL XYTCPPushSend(PXYTRANSPORT pt, PXYTCP_OVERLAPPED pto, SOCKET s)
{
	PXYBASE_OVERLAPPED pbo;
	DWORD numberofbytes;
	ULONG flags = MSG_PARTIAL;
	BOOL result;
#ifdef XYDEBUG
	int error;
#endif

	pbo = &pto->bo;

	ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

#ifdef XYDEBUG
	result = fn_WSASend(s, &pbo->wb, 1, &numberofbytes, flags, &pbo->o, NULL) != SOCKET_ERROR;
	if (!result)
	{
		error = fn_WSAGetLastError();
		result = error == WSA_IO_PENDING;
		if (!result)
		{
			OutputDebugError(_T("WSASend"), error);
		}
	}
#else
	result = fn_WSASend(s, &pbo->wb, 1, &numberofbytes, flags, &pbo->o, NULL) != SOCKET_ERROR || fn_WSAGetLastError() == WSA_IO_PENDING;
#endif

	// 这里如果失败了也不回收

	return(result);
}
BOOL XYTCPPushSend(PXYTRANSPORT pt, PXYTCP_NODE ptn, const char *buffer, int length, int segmentlength)
{
	PXYTCP_OVERLAPPED pto0;
	PXYTCP_OVERLAPPED pto1;
	PXYTCP_OVERLAPPED head;
	PXYTCP_OVERLAPPED rear = NULL;
	LPVOID context;
	int l;
	BOOL flag = FALSE;
	BOOL result = FALSE;

	context = GetTCPContext(ptn);

	if (segmentlength == 0)
	{
		EnterCriticalSection(&ptn->cs1);

		l = XYTCPMergeBuffer(pt, ptn, buffer, length);

		if (l > 0)
		{
			buffer += l;
			length -= l;
		}
	}
	else
	{
		flag = TRUE;
	}

	result = length == 0;
	if (!result)
	{
		EnterCriticalSection(&pt->cs4);

		pto1 = XYTCPLoadBuffer(pt, &pto0, context, buffer, length, segmentlength);

		LeaveCriticalSection(&pt->cs4);

		if (pto1 != NULL)
		{
			if (segmentlength > 0)
			{
				EnterCriticalSection(&ptn->cs1);

				flag = FALSE;
			}

			XYTCPOverlappedsAdd(ptn, pto0, pto1);

			result = TRUE;
			if (ptn->head1 == (LPVOID)pto0)
			{
				ptn->head1 = (PXYTCP_OVERLAPPED)pto0->bo.next;
				if (pto0 == pto1)
				{
					ptn->rear1 = ptn->head1;
				}
				else
				{
					pto1 = pto0;
				}

				result = XYTCPPushSend(pt, pto0, ptn->s);
				if (result)
				{
					ptn->sending++;
				}
			}

			if (!result)
			{
				if (ptn->rear1 != NULL)
				{
					head = (PXYTCP_OVERLAPPED)ptn->head1;
					rear = (PXYTCP_OVERLAPPED)ptn->rear1;
					ptn->head1 = ptn->rear1 = NULL;
				}
			}

			LeaveCriticalSection(&ptn->cs1);
			flag = TRUE;

			if (!result)
			{
				EnterCriticalSection(&pt->cs4);
				XYTCPOverlappedsPush(pt, pto0, pto1);
				if (rear != NULL)
				{
					XYTCPOverlappedsPush(pt, head, rear);
				}
				LeaveCriticalSection(&pt->cs4);
			}
		}
	}

	if (!flag)
	{
		LeaveCriticalSection(&ptn->cs1);
	}

	return(result);
}
BOOL XYTCPPopSend(PXYTRANSPORT pt, PXYTCP_OVERLAPPED pto, PXYTCP_NODE ptn, SOCKET s, UINT length)
{
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED next;
	PXYTCP_OVERLAPPED pto0;
	PXYTCP_OVERLAPPED pto1 = NULL;
	LPVOID overlapped;
	char *buffer;
	UINT l;
	UINT number;
	UINT returns;
	BOOL result;

	EnterCriticalSection(&ptn->cs1);
	next = XYTCPRemoveSent(ptn, pto, length);
	result = ptn->head0 != NULL && (next == NULL || XYTCPPushSend(pt, next, s));

	if (!result)
	{
		pto1 = XYTCPOverlappedsRemove(ptn, &pto0);
	}

	if (pto != next)
	{
		if (result)
		{
			ptn->sending--;
		}
		if (next != NULL)
		{
			if (result)
			{
				ptn->sending++;
			}
		}
	}

	LeaveCriticalSection(&ptn->cs1);

	if (pto != next)
	{
		if (next == NULL)
		{
			number = XYTRANSPORT_TCP_SEND0;
		}
		else
		{
			number = XYTRANSPORT_TCP_SEND1;
		}

		//
		pbo = &pto->bo;

		l = pto->bufferlength;
		buffer = pbo->wb.buf;
		pbo->wb.len = l;

		pbo->next = NULL;
		overlapped = (LPVOID)pto;
		returns = pt->procedure((LPVOID)pt, pt->parameter, GetTCPContext(ptn), &overlapped, ptn->listener, ptn->customdata, number, buffer, l);
		// 调用者回收
		//

		if (!result)
		{
			if (next != NULL)
			{
				EnterCriticalSection(&pt->cs4);
				XYTCPOverlappedPush(pt, next);
				LeaveCriticalSection(&pt->cs4);
			}
		}
	}

	if (pto1 != NULL)
	{
		XYTCPOverlappedsPush(pt, pto0, pto1);
	}

	return(result);
}

BOOL XYTCPPushAccept(PXYTRANSPORT pt, PXYLISTENER pl)
{
	GUID id0 = WSAID_ACCEPTEX;
	GUID id1 = WSAID_GETACCEPTEXSOCKADDRS;
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED pto;
	PXYTCP_NODE ptn;
	DWORD numberofbytes = 0;
	DWORD length0;
	DWORD length1;
	BOOL result = FALSE;
#ifdef XYDEBUG
	int error;
#endif

	EnterCriticalSection(&pt->cs2);
	ptn = XYTCPNodePop(pt, (LPVOID)pl, NULL, &pt->stacks[XYTCP_LIST_CLIENT1]);
	LeaveCriticalSection(&pt->cs2);

	if (ptn != NULL)
	{
		if (pt->lpfnAcceptEx != NULL || fn_WSAIoctl(ptn->s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id0, sizeof(id0), &pt->lpfnAcceptEx, sizeof(pt->lpfnAcceptEx), &numberofbytes, NULL, NULL) != SOCKET_ERROR)
		{
			if (pt->lpfnGetAcceptExSockAddrs != NULL || fn_WSAIoctl(ptn->s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id1, sizeof(id1), &pt->lpfnGetAcceptExSockAddrs, sizeof(pt->lpfnGetAcceptExSockAddrs), &numberofbytes, NULL, NULL) != SOCKET_ERROR)
			{
				EnterCriticalSection(&pt->cs4);
				pto = XYTCPOverlappedPop(pt);
				LeaveCriticalSection(&pt->cs4);

				if (pto != NULL)
				{
					pbo = &pto->bo;

					ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

					pbo->flags = XYTRANSPORT_TYPE_TCP_OPEN;

					pbo->context = GetTCPContext(ptn);

					length0 = sizeof(SOCKADDR_IN) + 16;
					//length1 = pt->bufferlength - length0 - length0;
					length1 = 0;

#ifdef XYDEBUG
					result = pt->lpfnAcceptEx(pl->s, ptn->s, pto->buffer, length1, length0, length0, &numberofbytes, &pbo->o);
					if (!result)
					{
						error = fn_WSAGetLastError();
						result = error == ERROR_IO_PENDING;
						if (!result)
						{
							OutputDebugError(_T("AcceptEx"), error);

							OutputDebugValue(_T("Sockets "), (int)pl->s, (int)ptn->s);
						}
					}
#else
					result = pt->lpfnAcceptEx(pl->s, ptn->s, pto->buffer, length1, length0, length0, &numberofbytes, &pbo->o) || fn_WSAGetLastError() == ERROR_IO_PENDING;
#endif

					if (!result)
					{
						EnterCriticalSection(&pt->cs4);
						XYTCPOverlappedPush(pt, pto);
						LeaveCriticalSection(&pt->cs4);
					}
				}
#ifdef XYDEBUG
				else
				{
					OutputDebugString(_T("申请OVERLAPPED失败 AcceptEx"));
				}
#endif
			}
		}

		if (!result)
		{
			EnterCriticalSection(&pt->cs2);
			XYTCPNodePush(&pt->stacks[XYTCP_LIST_CLIENT1], ptn);
			LeaveCriticalSection(&pt->cs2);

			InterlockedDecrement(&pt->temp0);
		}
	}
#ifdef FIND_TROJAN_BUG
	else
	{
		OutputDebugString(_T("申请Node失败 AcceptEx"));
	}
#endif

	return (result);
}

VOID XYTCPClearSendOverlappeds(PXYTRANSPORT pt, PXYTCP_NODE ptn, PXYTCP_OVERLAPPED pto)
{
	PXYTCP_OVERLAPPED pto0;
	PXYTCP_OVERLAPPED pto1;

	EnterCriticalSection(&ptn->cs1);
	pto1 = XYTCPOverlappedsRemove(ptn, &pto0);
	LeaveCriticalSection(&ptn->cs1);

	EnterCriticalSection(&pt->cs4);
	//if (pto != NULL)
	{
		XYTCPOverlappedPush(pt, pto);
	}
	if (pto1 != NULL)
	{
		XYTCPOverlappedsPush(pt, pto0, pto1);
	}
	LeaveCriticalSection(&pt->cs4);
}

BOOL XYTCPRecycle(PXYTRANSPORT pt, PXYLISTENER pl, PXYTCP_OVERLAPPED pto, PXYTCP_NODE ptn, UINT error)
{
	PXYBASE_OVERLAPPED pbo;
	LPVOID overlapped;
	LPVOID context;
	BOOL result = TRUE;
	UINT returns = 0;

	pbo = &pto->bo;

	if (pl == NULL || error == 0)
	{
		//
		pbo->wb.buf = (char *)ptn->customdata;
		//
		pbo->wb.len = error;

		pbo->next = NULL;
		overlapped = (LPVOID)pto;
		returns = pt->procedure((LPVOID)pt, pt->parameter, GetTCPContext(ptn), &overlapped, ptn->listener, ptn->customdata, XYTRANSPORT_TCP_CLOSE, NULL, error);
		// 调用者回收
	}
	else
	{
		context = pbo->context;

		XYTCPReleaseOverlapped(pt, (LPVOID)pto);

		XYTCPReleaseContext(pt, context);
	}

	return(result);
}
VOID XYTCPAchieved(PXYTRANSPORT pt, PXYLISTENER pl, PXYTCP_OVERLAPPED pto, PXYTCP_NODE ptn, SOCKET s, UINT type, UINT length)
{
	BOOL flag = FALSE;

	if (type == XYTRANSPORT_TYPE_TCP_RECV)
	{
		XYTCPPopReceive(pt, pto, ptn, length);
		if (length > 0)
		{
			flag = XYTCPPushReceive(pt, ptn, s);
		}
		else
		{
			flag = FALSE;
		}
	}
	else
	{
		if (length > 0)
		{
			flag = XYTCPPopSend(pt, pto, ptn, s, length);
		}
		else
		{
			XYTCPClearSendOverlappeds(pt, ptn, pto);

			flag = FALSE;
		}
	}

	if (!flag)
	{
#ifdef FIND_TROJAN_BUG
#endif

		if (!XYTCPDisconnect(pt, pl, ptn, s, type))
		{
			//OutputDebugString(_T("disconnect error(send|recv)"));
		}
	}
}

VOID XYTCPPopConnect(PXYTRANSPORT pt, PXYLISTENER pl, PXYTCP_OVERLAPPED pto, PXYTCP_NODE ptn)
{
	PXYBASE_OVERLAPPED pbo;
	LPVOID overlapped;
	XYTCP_HOST_INFO hi;
	DWORD length0;
	DWORD length1;
	LPVOID pointer;
	LPVOID customdata;
	UINT returns;

	pbo = &pto->bo;

	if (pl != NULL)
	{
		hi.customdata = ((PXYLISTENER)ptn->listener)->customdata;

		hi.lplocaladdress = NULL;
		hi.lpremoteaddress = NULL;
		hi.locallength = sizeof(SOCKADDR_IN);
		hi.remotelength = sizeof(SOCKADDR_IN);
		length0 = sizeof(SOCKADDR_IN) + 16;
		//length1 = pt->bufferlength - length0 - length0;
		length1 = 0;
		pt->lpfnGetAcceptExSockAddrs((LPVOID)pto->buffer, length1, length0, length0, (LPSOCKADDR*)&hi.lplocaladdress, &hi.locallength, (LPSOCKADDR*)&hi.lpremoteaddress, &hi.remotelength);

		pointer = (LPVOID)&hi;

		customdata = hi.customdata;
	}
	else
	{
		pointer = NULL;

		customdata = ptn->customdata;
	}

	//
	pbo->wb.buf = (char *)customdata;
	//

	pbo->next = NULL;
	overlapped = (LPVOID)pto;
	returns = pt->procedure((LPVOID)pt, pt->parameter, GetTCPContext(ptn), &overlapped, pointer, customdata, XYTRANSPORT_TCP_CONNECT, pbo->wb.buf, pbo->wb.len);
	// 调用者回收

	if (pointer != NULL)
	{
		ptn->customdata = hi.customdata;
	}
}

DWORD WINAPI XYTransportWorkProc(LPVOID parameter)
{
	PXYTRANSPORT pt = (PXYTRANSPORT)parameter;
	HANDLE hcompletion = pt->hcompletion;
	LPOVERLAPPED po;
	PXYBASE_OVERLAPPED pbo;
	PXYUDP_OVERLAPPED puo;
	PXYTCP_OVERLAPPED pto;
	PXYUDP_NODE pun;
 	PXYTCP_NODE ptn;
	PXYLISTENER pl;
	ULONG_PTR completionkey;
	DWORD numberofbytes;
	SOCKET s;
	BOOL flag;
	CRITICAL_SECTION *pcs0;
	LPVOID *head;
	UINT type;
	UINT count;
	UINT error;

	while(pt->working)
 	{
		flag = GetQueuedCompletionStatus(hcompletion, &numberofbytes, &completionkey, &po, INFINITE);

		if (po != NULL)
		{
			pbo = (PXYBASE_OVERLAPPED)CONTAINING_RECORD(po, XYBASE_OVERLAPPED, o);

			s = (SOCKET)completionkey;

			type = pbo->flags;

			//if (!flag)
			{
				//numberofbytes = 0;
			}

			if (type < XYTRANSPORT_TYPE_RANGE_TCP)
			{
				puo = (PXYUDP_OVERLAPPED)CONTAINING_RECORD(pbo, XYUDP_OVERLAPPED, bo);

				pun = GetUDPNode(pbo->context);

				switch (type)
				{
				case XYTRANSPORT_TYPE_UDP_RECV:
					XYUDPPopReceive(pt, puo, pun, numberofbytes);
					if (!flag || !XYUDPPushReceive(pt, NULL, pun, s))
					{
						if (numberofbytes == 0)
						{
						}
					}
					break;
				case XYTRANSPORT_TYPE_UDP_SEND0:
					if (!flag || !XYUDPPushReceives(pt, &count, pun, s))
					{
						if (numberofbytes > 0)
						{
							numberofbytes = 0;
						}
					}
				case XYTRANSPORT_TYPE_UDP_SEND1:
					XYUDPPopSendTo(pt, puo, pun, s, numberofbytes);
					break;
				default:
					break;
				}

				//if (numberofbytes == 0)
				{
					XYUDPTryRecycle(pt, pun);
				}
			}
			else
			{
				pto = (PXYTCP_OVERLAPPED)CONTAINING_RECORD(pbo, XYTCP_OVERLAPPED, bo);

				ptn = GetTCPNode(pbo->context);

				pl = (PXYLISTENER)ptn->listener;

				switch (type)
				{
				case XYTRANSPORT_TYPE_TCP_OPEN:
					if (flag)
					{
						if (pl != NULL)
						{
							if (XYTCPPushAccept(pt, pl))
							{
								//OutputDebugString(L"再Post Accept成功！", 90);
							}
							else
							{
								//OutputDebugString(_T("再Post Accept失败！"));
							}

							//if ((ptn->flags&XYTRANSPORT_FLAG_TCP_BIRTH) == 0)
							{
								//XYDebugWrite3(L"socket.txt", ptn->s, s, pl->s);
								fn_setsockopt(ptn->s, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&s, sizeof(SOCKET));
							}

							s = ptn->s;

							pcs0 = &pl->cs;
							head = &pl->head;
						}
						else
						{
							//if ((ptn->flags&XYTRANSPORT_FLAG_TCP_BIRTH) == 0)
							{
								fn_setsockopt(s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
							}

							pcs0 = &pt->cs0;
							head = &pt->heads[XYTCP_LIST_CLIENT0];
						}

						EnterCriticalSection(pcs0);
						XYTCPNodeAdd(head, ptn);
						LeaveCriticalSection(pcs0);

						// 先处理连接事件，再投递接收
						XYTCPPopConnect(pt, pl, pto, ptn);

						// 连接事件那里，用户马上发送数据包并且发送成功了

						if (!XYTCPPushReceive(pt, ptn, s))
						{
							XYTCPDisconnect(pt, pl, ptn, s, XYTRANSPORT_TYPE_TCP_RECV);
						}
						break;
					}
				case XYTRANSPORT_TYPE_TCP_CLOSE:
					//OutputDebugValue(_T("GQCSs TCP Close"), (int)type, flag);

					if (flag)
					{
						error = 0;
					}
					else
					{
						error = XYTRANSPORT_ERROR_ABORT0;
					}
					//OutputDebugValue(_T("GQCSs TCP Close"), (int)type, flag, error);
					XYTCPRecycle(pt, pl, pto, ptn, error);
					break;
				case XYTRANSPORT_TYPE_TCP_RECV:
				case XYTRANSPORT_TYPE_TCP_SEND:
					//OutputDebugValue(_T("GQCSs TCP Send Recv"), (int)type, flag);

					XYTCPAchieved(pt, pl, pto, ptn, s, type, numberofbytes);
					break;
				default:
					break;
				}
			}
		}
		else
		{
#ifdef FIND_TROJAN_BUG
			OutputDebugValue(_T("get quit message"), GetCurrentThreadId(), flag);
#endif

			break;
		}
	}

#ifdef FIND_TROJAN_BUG
	OutputDebugValue(_T("quit thread"), GetCurrentThreadId());
#endif

	return(0);
}

#ifdef XYDYNAMIC_LOAD
BOOL XYTransportStartup(PXYTRANSPORT pt, LPVOID parameter, UINT pagesize, UINT contextsize, UINT count, XYTRANSPORT_PROCEDURE procedure, HMODULE hwinsock)
#else
BOOL XYTransportStartup(PXYTRANSPORT pt, LPVOID parameter, UINT pagesize, UINT contextsize, UINT count, XYTRANSPORT_PROCEDURE procedure)
#endif
{
	UINT i;
	LPVOID nodes[512];
	BOOL result = FALSE;

	pt->hcompletion = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, count);
	if (pt->hcompletion != INVALID_HANDLE_VALUE)
	{
		pt->heap = GetProcessHeap();

		pt->stack0 = NULL;
		pt->stack1 = NULL;
		pt->udp_head = NULL;

		pt->heads[XYTCP_LIST_LISTENER] = NULL;
		pt->stacks[XYTCP_LIST_LISTENER] = NULL;
		pt->heads[XYTCP_LIST_CLIENT0] = NULL;
		pt->stacks[XYTCP_LIST_CLIENT0] = NULL;
		pt->stacks[XYTCP_LIST_CLIENT1] = NULL;

		pt->stack = NULL;

		pt->head = NULL;
		pt->rear = NULL;

		pt->parameter = parameter;

		pt->procedure = procedure;

#ifdef XYDYNAMIC_LOAD
		pt->pfn_WSAGetLastError = (t_WSAGetLastError)GetProcAddress(hwinsock, "WSAGetLastError");
		pt->pfn_WSAIoctl = (t_WSAIoctl)GetProcAddress(hwinsock, "WSAIoctl");
		pt->pfn_WSASendTo = (t_WSASendTo)GetProcAddress(hwinsock, "WSASendTo");
		pt->pfn_WSARecvFrom = (t_WSARecvFrom)GetProcAddress(hwinsock, "WSARecvFrom");
		pt->pfn_WSASend = (t_WSASend)GetProcAddress(hwinsock, "WSASend");
		pt->pfn_WSARecv = (t_WSARecv)GetProcAddress(hwinsock, "WSARecv");
		pt->pfn_WSASocket = (t_WSASocket)GetProcAddress(hwinsock, "WSASocketW");
		pt->pfn_closesocket = (t_closesocket)GetProcAddress(hwinsock, "closesocket");
		pt->pfn_shutdown = (t_shutdown)GetProcAddress(hwinsock, "shutdown");
		pt->pfn_htons = (t_htons)GetProcAddress(hwinsock, "htons");
		pt->pfn_inet_addr = (t_inet_addr)GetProcAddress(hwinsock, "inet_addr");
		pt->pfn_setsockopt = (t_setsockopt)GetProcAddress(hwinsock, "setsockopt");
		pt->pfn_bind = (t_bind)GetProcAddress(hwinsock, "bind");
		pt->pfn_listen = (t_listen)GetProcAddress(hwinsock, "listen");
#endif

		pt->lpfnAcceptEx = NULL;
		pt->lpfnGetAcceptExSockAddrs = NULL;
		pt->lpfnConnectEx = NULL;
		pt->lpfnDisconnectEx = NULL;

		pt->bufferlength = pagesize;
		pt->contextsize = contextsize;

		pt->temp0 = 0;
		pt->temp1 = 0;
		pt->stemp0 = 0;
		pt->stemp1 = 0;

		//XYTCPNodesPush(pt, NULL, &pt->stacks[XYTCP_LIST_CLIENT0], nodes, 128);
		XYListenersPush(pt, &pt->stacks[XYTCP_LIST_LISTENER], nodes, 1);
		XYTCPOverlappedsPush(pt, nodes, 512);

		pt->working = TRUE;

		pt->hthreads = (HANDLE *)XYAlloc(pt->heap, sizeof(HANDLE)*count);
		pt->count = count;
		for (i = 0; i < count; i++)
		{
			pt->hthreads[i] = CreateThread(NULL, 0, XYTransportWorkProc, (LPVOID)pt, 0, NULL);
		}

		InitializeCriticalSection(&pt->udp_cs0);
		InitializeCriticalSection(&pt->udp_cs1);
		InitializeCriticalSection(&pt->udp_cs2);

		InitializeCriticalSection(&pt->cs0);
		InitializeCriticalSection(&pt->cs1);
		InitializeCriticalSection(&pt->cs2);
		InitializeCriticalSection(&pt->cs3);
		InitializeCriticalSection(&pt->cs4);
		InitializeCriticalSection(&pt->cs5);
		InitializeCriticalSection(&pt->cs6);
		InitializeCriticalSection(&pt->cs7);

		result = TRUE;
	}

	return(result);
}

VOID XYTransportStop(PXYTRANSPORT pt)
{
	//IntermentlockedChange(
	XYTCPNodesStop(pt, NULL, &pt->heads[XYTCP_LIST_CLIENT0], &pt->cs0);

	XYListenersStop(pt, &pt->heads[XYTCP_LIST_LISTENER],&pt->cs6);
}
VOID XYTransportCleanup(PXYTRANSPORT pt)
{
	UINT i, count;

	//IntermentlockedChange(
	pt->working = FALSE;

	XYTCPNodesStop(pt, NULL, &pt->heads[XYTCP_LIST_CLIENT0], &pt->cs0);

	XYListenersStop(pt, &pt->heads[XYTCP_LIST_LISTENER],&pt->cs6);

	count=pt->count;
	for (i = 0; i < count; i++)
	{
		PostQueuedCompletionStatus(pt->hcompletion, 0, (DWORD)NULL, NULL);
	}

	for (i = 0; i < count; i++)
	{
		if (pt->hthreads[i] != INVALID_HANDLE_VALUE)
		{
			WaitForSingleObject(pt->hthreads[i], INFINITE);
			CloseHandle(pt->hthreads[i]);
			pt->hthreads[i] = INVALID_HANDLE_VALUE;
		}
	}
	XYFree(pt->heap, (LPVOID)pt->hthreads);

	CloseHandle(pt->hcompletion);

	XYTCPOverlappedsClear(pt);
	XYListenersClear(pt, pt->stacks[XYTCP_LIST_LISTENER]);
	XYTCPNodesClear(pt, &pt->stacks[XYTCP_LIST_CLIENT0]);
	XYTCPNodesClear(pt, &pt->stacks[XYTCP_LIST_CLIENT1]);

	DeleteCriticalSection(&pt->udp_cs0);
	DeleteCriticalSection(&pt->udp_cs2);
	DeleteCriticalSection(&pt->udp_cs2);

	DeleteCriticalSection(&pt->cs0);
	DeleteCriticalSection(&pt->cs1);
	DeleteCriticalSection(&pt->cs2);
	DeleteCriticalSection(&pt->cs3);
	DeleteCriticalSection(&pt->cs4);
	DeleteCriticalSection(&pt->cs5);
	DeleteCriticalSection(&pt->cs6);
	DeleteCriticalSection(&pt->cs7);
}

BOOL XYTCPConnect(PXYTRANSPORT pt, LPVOID customdata, const CHAR *host, USHORT port, PVOID lpsendbuffer, DWORD senddatalength)
{
	GUID id = WSAID_CONNECTEX;
	DWORD numberofbytes = 0;
	PXYBASE_OVERLAPPED pbo;
	PXYTCP_OVERLAPPED pto;
	PXYTCP_NODE ptn;
	SOCKADDR_IN name;
	BOOL result = FALSE;
#ifdef XYDEBUG
	int error;
#endif

	EnterCriticalSection(&pt->cs1);
	ptn = XYTCPNodePop(pt, NULL, customdata, &pt->stacks[XYTCP_LIST_CLIENT0]);
	LeaveCriticalSection(&pt->cs1);

	if (ptn != NULL)
	{
		if (pt->lpfnConnectEx != NULL || fn_WSAIoctl(ptn->s, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &pt->lpfnConnectEx, sizeof(pt->lpfnConnectEx), &numberofbytes, NULL, NULL) != SOCKET_ERROR)
		{
			EnterCriticalSection(&pt->cs4);
			pto = XYTCPOverlappedPop(pt);
			LeaveCriticalSection(&pt->cs4);

			if (pto != NULL)
			{
				pbo = &pto->bo;

				ZeroMemory(&pbo->o, sizeof(OVERLAPPED));

				pbo->flags = XYTRANSPORT_TYPE_TCP_OPEN;

				pbo->context = GetTCPContext(ptn);

				ZeroMemory(&name, sizeof(name));
				name.sin_family = AF_INET;
				//name.sin_addr.S_un.S_addr = 
				name.sin_port = fn_htons(port);
				//fn_InetPtonA(AF_INET, host, (PVOID)&name.sin_addr);
				name.sin_addr.S_un.S_addr = fn_inet_addr(host);
				if (name.sin_addr.S_un.S_addr != INADDR_NONE)
				{
					numberofbytes = 0;

#ifdef XYDEBUG
					result = pt->lpfnConnectEx(ptn->s, (SOCKADDR *)&name, sizeof(name), lpsendbuffer, senddatalength, &numberofbytes, &pbo->o);
					if(!result)
					{
						error = fn_WSAGetLastError();
						result = error == ERROR_IO_PENDING;
						if (!result)
						{
							OutputDebugError(_T("ConnectEx"), error);
						}
					}
#else
					result = pt->lpfnConnectEx(ptn->s, (SOCKADDR *)&name, sizeof(name), lpsendbuffer, senddatalength, &numberofbytes, &pbo->o) || fn_WSAGetLastError() == ERROR_IO_PENDING;
#endif
				}

				if (!result)
				{
					EnterCriticalSection(&pt->cs4);
					XYTCPOverlappedPush(pt, pto);
					LeaveCriticalSection(&pt->cs4);
				}
			}
#ifdef FIND_TROJAN_BUG
			else
			{
				OutputDebugString(_T("申请Overlapped失败 ConnectEx"));
			}
#endif
		}

		if (!result)
		{
			EnterCriticalSection(&pt->cs1);
			XYTCPNodePush(&pt->stacks[XYTCP_LIST_CLIENT0], ptn);
			LeaveCriticalSection(&pt->cs1);

			InterlockedDecrement(&pt->temp0);
		}
	}
#ifdef FIND_TROJAN_BUG
	else
	{
		OutputDebugString(_T("申请Node失败 ConnectEx"));
	}
#endif

	return(result);
}

LPVOID XYTCPListen(PXYTRANSPORT pt, LPVOID customdata, const CHAR *host, USHORT port, UINT number)
{
	PXYLISTENER pl = NULL;
	SOCKADDR_IN name;
	SOCKET s;
	UINT i;

	s = fn_WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (s != INVALID_SOCKET)
	{
		ZeroMemory(&name, sizeof(name));
		name.sin_family = AF_INET;
		if (host == NULL)
		{
			name.sin_addr.S_un.S_addr = INADDR_ANY;
		}
		else
		{
			//fn_InetPtonA(AF_INET, host, (PVOID)&name.sin_addr);
			//if (inet_ntop(sinp->sin_family, &sinp->sin_addr, seraddr, INET_ADDRSTRLEN) != NULL)
		}
		name.sin_port = fn_htons(port);
		if (fn_bind(s, (const SOCKADDR *)&name, sizeof(name)) == 0)
		{
			if (fn_listen(s, SOMAXCONN) != SOCKET_ERROR)
			{
				if (CreateIoCompletionPort((HANDLE)s, pt->hcompletion, (ULONG_PTR)s, 0) == pt->hcompletion)
				{
					EnterCriticalSection(&pt->cs3);
					pl = XYListenerPop(pt, customdata, &pt->stacks[XYTCP_LIST_LISTENER]);
					LeaveCriticalSection(&pt->cs3);

					if (pl != NULL)
					{
						pl->s = s;

						if (number == 0)
						{
							number = 256;
						}
						for (i = 0; i < number; i++)
						{
							if (!XYTCPPushAccept(pt, pl))
							{
								break;
							}
						}

						if (i > 0)
						{
							EnterCriticalSection(&pt->cs7);
							XYListenerAdd(&pt->heads[XYTCP_LIST_LISTENER], pl);
							LeaveCriticalSection(&pt->cs7);

							s = INVALID_SOCKET;
						}
						else
						{
							EnterCriticalSection(&pt->cs3);
							XYListenerPush(&pt->stacks[XYTCP_LIST_LISTENER], pl);
							LeaveCriticalSection(&pt->cs3);
							pl = NULL;
						}
					}

					if (s != INVALID_SOCKET)
					{
						CancelIo((HANDLE)s);
					}
				}
			}
		}

		if (s != INVALID_SOCKET)
		{
			fn_closesocket(s);
		}
	}

	return((LPVOID)pl);
}

BOOL XYTCPSend(PXYTRANSPORT pt, LPVOID context, const char *buffer, int length, int segmentlength)
{
	PXYTCP_NODE ptn;
	BOOL result = FALSE;

	if (segmentlength == -1)
	{
		segmentlength = pt->bufferlength;
	}

	ptn = GetTCPNode(context);

	EnterCriticalSection(&ptn->cs1);
	if ((ptn->flags&XYTRANSPORT_FLAG_SHUTDOWN1) == 0 && (ptn->flags&XYTRANSPORT_FLAG_SHUTDOWN0) == 0)
	{
		result = XYTCPPushSend(pt, GetTCPNode(context), buffer, length, segmentlength);
	}
	LeaveCriticalSection(&ptn->cs1);
	return(result);
}

BOOL XYTCPDisconnect(PXYTRANSPORT pt, LPVOID context)
{
	PXYTCP_NODE ptn;
	PXYLISTENER pl;
	CRITICAL_SECTION *pcs0;
	LPVOID *head;
	BOOL result = FALSE;

	ptn = GetTCPNode(context);

	pl = (PXYLISTENER)ptn->listener;
	if (pl != NULL)
	{
		pcs0 = &pl->cs;
		head = &pl->head;
	}
	else
	{
		pcs0 = &pt->cs0;
		head = &pt->heads[XYTCP_LIST_CLIENT0];
	}

	EnterCriticalSection(&ptn->cs1);
	if ((ptn->flags&XYTRANSPORT_FLAG_SHUTDOWN1) == 0 && (ptn->flags&XYTRANSPORT_FLAG_SHUTDOWN0) == 0)
	{
		ptn->flags |= XYTRANSPORT_FLAG_SHUTDOWN1;

		EnterCriticalSection(pcs0);
		XYTCPNodeRemove(head, ptn);
		LeaveCriticalSection(pcs0);

		result = fn_shutdown(ptn->s, SD_BOTH) == 0;
	}
	LeaveCriticalSection(&ptn->cs1);
	return(result);
}

UINT XYListenerStop(PXYTRANSPORT pt, LPVOID listener)
{
	return(XYListenerStop(pt, (PXYLISTENER)listener));
}
UINT XYListenersStop(PXYTRANSPORT pt)
{
	return(XYListenersStop(pt, &pt->heads[XYTCP_LIST_LISTENER], &pt->cs6));
}

VOID XYTCPReleaseOverlapped(PXYTRANSPORT pt, LPVOID overlapped)
{
	EnterCriticalSection(&pt->cs4);
	XYTCPOverlappedPush(pt, (PXYTCP_OVERLAPPED)overlapped);
	LeaveCriticalSection(&pt->cs4);
}
VOID XYTCPReleaseContext(PXYTRANSPORT pt, LPVOID context)
{
	PXYTCP_NODE ptn;
	PXYLISTENER pl;
	
	ptn = GetTCPNode(context);

	pl = (PXYLISTENER)ptn->listener;
	
	XYTCPReleaseNode(pt, pl, ptn);
}

VOID XYOverlappedEnqueue(LPVOID *head, LPVOID *rear, LPVOID *overlappeds)
{
	if (*rear == NULL)
	{
		*head = overlappeds[0];
	}
	else
	{
		((PXYBASE_OVERLAPPED)*rear)->next = overlappeds[0];
	}
	//*rear = overlappeds[1];
	*rear = overlappeds[0];
}
LPVOID XYOverlappedDequeue(LPVOID *head, LPVOID *rear)
{
	LPVOID result = NULL;

	if (head != NULL)
	{
		result = *head;
	}
	if (result != NULL)
	{
		*head = ((PXYBASE_OVERLAPPED)result)->next;
		if (*head == NULL)
		{
			*rear = NULL;
		}
	}
	return(result);
}

LPVOID XYGetOverlappedContext(LPVOID overlapped)
{
	return(((PXYBASE_OVERLAPPED)overlapped)->context);
}
const char *XYGetOverlappedBuffer(LPVOID overlapped, int *length)
{
	*length = ((PXYBASE_OVERLAPPED)overlapped)->wb.len;
	return(((PXYBASE_OVERLAPPED)overlapped)->wb.buf);
}

VOID XYSetOverlappedType(LPVOID overlapped, UINT type)
{
	((PXYBASE_OVERLAPPED)overlapped)->flags = type;
}
UINT XYGetOverlappedType(LPVOID overlapped)
{
	return(((PXYBASE_OVERLAPPED)overlapped)->flags);
}

LPVOID XYGetOverlappedCustomData(LPVOID overlapped)
{
	return((LPVOID)((PXYBASE_OVERLAPPED)overlapped)->wb.buf);
}

UINT XYTCPGetStackCount(PXYTRANSPORT pt)
{
	PXYTCP_OVERLAPPED pto;
	UINT count = 0;

	pto = (PXYTCP_OVERLAPPED)pt->stack;
	while (pto != NULL)
	{
		count++;

		pto = (PXYTCP_OVERLAPPED)pto->bo.next;
	}
	return(count);
}
//---------------------------------------------------------------------------

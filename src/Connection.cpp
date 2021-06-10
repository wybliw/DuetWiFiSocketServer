/*
 * Socket.cpp
 *
 *  Created on: 11 Apr 2017
 *      Author: David
 */

#if ESP32
#include "Connection.h"
#include "algorithm"			// for std::min
#include "Arduino.h"			// for millis
#include "Config.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "Listener.h"
const uint32_t MaxWriteTime = 2000;		// how long we wait for a write operation to complete before it is cancelled
const uint32_t MaxAckTime = 4000;		// how long we wait for a connection to acknowledge the remaining data before it is closed

// Public interface
Connection::Connection(uint8_t num) :
#ifdef EXTENDED_LISTEN
 number(0xff),
#else
 number(num),
#endif
 state(ConnState::free), closeTimer(0), inCnt(0), inPos(0), outCnt(0), outPos(0), 
 	  localPort(0), remotePort(0), remoteIp(0),
	  sock(-1), totalRead(0)
{
}

void ICACHE_RAM_ATTR Connection::GetStatus(ConnStatusResponse& resp)
{
	if ((state != ConnState::connected) && (state != ConnState::free))
	{
		//debugPrintf("Getstatus num %d %d state %d\n", number, sock, state);
	}
	//Poll();
	resp.socketNumber = number;
	resp.state = state;
	resp.bytesAvailable = CanRead();
	resp.writeBufferSpace = CanWrite();
	resp.localPort = localPort;
	resp.remotePort = remotePort;
	resp.remoteIp = remoteIp;
}

// Close the connection gracefully
void Connection::Close()
{
	//debugPrintf("Close %d state %d\n", sock, state);
	switch(state)
	{
	case ConnState::connected:						// both ends are still connected
		if (outCnt > 0)
		{
			//debugPrintf("outCnt is %d\n", outCnt);
			closeTimer = millis();
			SetState(ConnState::closePending);		// wait for the remaining data to be sent before closing
			//shutdown(sock, SHUT_RD);
			break;
		}
		[[fallthrough]];
	case ConnState::otherEndClosed:					// the other end has already closed the connection
	case ConnState::closeReady:						// the other end has closed and we were already closePending
	default:
	//debugPrintf("closing %d\n", sock);										// should not happen
		close(sock);
		SetState(ConnState::free);
		break;

	case ConnState::closePending:					// we already asked to close
		// Should not happen, but if it does just let the close proceed when sending is complete or timeout
		break;
	}
}

// Terminate the connection.
// If 'external' is true then the Duet main processor has requested termination, so we free up the connection.
// Otherwise it has failed because of an internal error, and we set the state to 'aborted'. The Duet main processor will see this and send a termination request,
// which will free it up.
void Connection::Terminate(bool external)
{
	debugPrintf("Terminate num %d sock %d external %d\n", number, sock, external);
	if (state != ConnState::free && state != ConnState::aborted)
	{
		//debugPrintf("Closing num %d sock %d\n", number, sock);
		close(sock);
	}
	SetState((external) ? ConnState::free : ConnState::aborted);
}

void ICACHE_RAM_ATTR Connection::WritePoll()
{
	if (outCnt > 0)
	{
		int ret = write(sock, outBuf + outPos, outCnt);
		if (ret < 0)
		{
			if (errno == EWOULDBLOCK)
				return;
			debugPrintf("Write %d len %d failed with %d\n", sock, outCnt, errno);
			Terminate(false);
		}
		//debugPrintf("sock %d outCnt %d written %d outPos %d\n", sock, outCnt, ret, outPos);
		outCnt -= ret;
		if (outCnt == 0)
			outPos = 0;
		else
			outPos += ret;
	}
}

void ICACHE_RAM_ATTR Connection::ReadPoll()
{
	size_t len = RBUFFER_SIZE - (inPos + inCnt);
	if (len > 0)
	{
		int ret = read(sock, inBuf+inPos+inCnt, len);
		if (ret < 0)
		{
			if (errno == EWOULDBLOCK)
				return;
			debugPrintf("Read %d len %d failed with %d\n", sock, len, errno);
			Terminate(false);
		}
		else if (ret == 0)
		{
			debugPrintf("poll read %d other end closed\n", sock);
			SetState(ConnState::otherEndClosed);
		}
		else
		{
			totalRead += ret;
			inCnt += ret;
			//debugPrintf("Poll %d ret is %d inCnt %d\n", sock, ret, inCnt);
		}
	}
}
		
// Perform housekeeping tasks
void ICACHE_RAM_ATTR Connection::Poll()
{
#ifdef EXTENDED_LISTEN
	if (state == ConnState::free)
		return;
	// Are we waiting to be made public?
	if (number == 0xff)
		MakePublic();
#endif
//debugPrintf("Poll sock %d state %d\n", sock, state);
	if (state == ConnState::connected)
	{
		if (inPos + inCnt < RBUFFER_SIZE) ReadPoll();
		if (outCnt > 0) WritePoll();
#if 0
		if (totalRead > 100000)
		{
			debugPrintf("Dumping data cnt %d\n", totalRead);
    int opts = fcntl(sock, F_GETFL, 0);

	if (opts < 0)
	{
		debugPrintf("Could not get socket flags %d", errno);
	}
	else if (fcntl(sock, F_SETFL, opts & ~O_NONBLOCK) < 0)
	{
		debugPrintf("Could not set non blocking flag %d", errno);
	}
			uint32_t start = millis();
			while(state == ConnState::connected)
			{
				//delay(1);
				inCnt = inPos = 0;
				ReadPoll();
			}
			debugPrintf("End dump cnt %d time %d\n", totalRead, millis() - start);
		}
#endif
	}
	else if (state == ConnState::closeReady)
	{
		// Deferred close, possibly outside the ISR
		//debugPrintf("poll closing %d\n", sock);
		Close();
	}
	else if (state == ConnState::closePending)
	{
		if (outCnt > 0)
		{
			WritePoll();
			if (outCnt == 0)
				SetState(ConnState::closeReady);
			else if (millis() - closeTimer >= MaxAckTime)
			{
				debugPrint("Unable to send data force close\n");
				Terminate(false);
			}
		}
		else
			if (millis() - closeTimer >= MaxAckTime)
			{
				debugPrint("Unable to send data force close\n");
				SetState(ConnState::closeReady);
			}
//		{
//			// The acknowledgement timer has expired, abort this connection
//			SetState(ConnState::closeReady);
//		}
	}
}


size_t ICACHE_RAM_ATTR Connection::Write(const uint8_t *data, size_t length, bool doPush, bool closeAfterSending)
{
	//debugPrintf("conn %d sock %d write len %d close %d\n", number, sock, length, closeAfterSending);
	if (state != ConnState::connected)
	{
		return 0;
	}
	size_t len = WBUFFER_SIZE - (outPos + outCnt);
	if (len > length) len = length;
	memcpy(outBuf + outCnt, data, len);
	outCnt += len;
	//debugPrintf("write end outCnt %d\n", outCnt);
	return len;
}

size_t ICACHE_RAM_ATTR Connection::CanWrite() const
{
	// Return the amount of free space in the write buffer
	// Note: we cannot necessarily write this amount, because it depends on memory allocations being successful.
	return (state == ConnState::connected) ? WBUFFER_SIZE - (outPos + outCnt) : 0;
}

size_t ICACHE_RAM_ATTR Connection::Read(uint8_t *data, size_t length)
{
	int ret = read(sock, data, length);
	if (ret < 0)
	{
		if (errno == EWOULDBLOCK)
			return 0;
		debugPrintf("Read %d len %d failed with %d\n", sock, length, errno);
		Terminate(false);
		ret = 0;
	}
	else if (ret == 0)
	{
		debugPrintf("poll read %d other end closed\n", sock);
		SetState(ConnState::otherEndClosed);
	}
	else
	{
		totalRead += ret;
		//debugPrintf("Poll %d ret is %d inCnt %d\n", sock, ret, inCnt);
	}
	return ret;
}

size_t ICACHE_RAM_ATTR Connection::CanRead() const
{
	if (state != ConnState::connected)
		return inCnt;
	uint32_t result;
	int ret = ioctl(sock, FIONREAD, &result);
	if (ret < 0)
	{
		debugPrintf("ioctl error %d\n", errno);
		return 0;
	}
	else
	{
		//debugPrintf("socket %d bytes available %d\n", sock, result);
	}
	return result + inCnt;
}

size_t ICACHE_RAM_ATTR Connection::Avail()
{
	return inCnt;
}

uint8_t * ICACHE_RAM_ATTR Connection::ReadAvail(size_t len)
{
	uint8_t *ret = inBuf + inPos;
	inCnt -= len;
	if (inCnt == 0)
		inPos = 0;
	else
		inPos += len;
	return ret;
}

void Connection::Report()
{
	// The following must be kept in the same order as the declarations in class ConnState
	static const char* const connStateText[] =
	{
		"free",
		"connecting",			// socket is trying to connect
		"connected",			// socket is connected
		"remoteClosed",			// the other end has closed the connection

		"aborted",				// an error has occurred
		"closePending",			// close this socket when sending is complete
		"closeReady"			// about to be closed
	};

	const unsigned int st = (int)state;
	ets_printf("%s", (st < ARRAY_SIZE(connStateText)) ? connStateText[st]: "unknown");
	if (state != ConnState::free)
	{
		ets_printf(" %u, %u, %u.%u.%u.%u", localPort, remotePort, remoteIp & 255, (remoteIp >> 8) & 255, (remoteIp >> 16) & 255, (remoteIp >> 24) & 255);
	}
}

#ifdef EXTENDED_LISTEN
void Connection::MakePublic()
{
	// make this connection public if we can
	for(uint32_t i = 0; i < MaxPublicConnections; i++)
	{
		// If one of our public connections is not in use, use that slot
		if (publicConnections[i]->state == ConnState::free)
		{
			// Found a public socket that is not in use
			publicConnections[i]->number = 0xff;
			number = i;
			publicConnections[i] = this;
			return;
		}
	}
}
#endif

// Callback functions
int Connection::Accept(int s)
{
	//debugPrintf("Accept sock %d on conn %d\n", s, number);
	sock = s;
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getpeername(s, (struct sockaddr*)&addr, &len);
    struct sockaddr_in *sa = (struct sockaddr_in *)&addr;
	remotePort = ntohs(sa->sin_port);
	remoteIp = sa->sin_addr.s_addr;
    len = sizeof addr;
    getsockname(s, (struct sockaddr*)&addr, &len);
    sa = (struct sockaddr_in *)&addr;
	localPort = ntohs(sa->sin_port);
IPAddress ip(remoteIp);
//debugPrintf("Ip address %d %d %d %d\n", ip[0], ip[1], ip[2], ip[3]);
//debugPrintf("Local port is %x, remote %x Ip %x\n", (unsigned)localPort, (unsigned)remotePort, (unsigned)remoteIp);
	closeTimer = 0;
	inCnt = outCnt = inPos = outPos = 0;
	totalRead = 0;
    int flags = 1; 
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) < 0) 
	{
		debugPrintf("Set NO delay error %d\n", errno);
	}
    int opts = fcntl(s, F_GETFL, 0);

	if (opts < 0)
	{
		debugPrintf("Could not get socket flags %d", errno);
	}
	else if (fcntl(s, F_SETFL, opts | O_NONBLOCK) < 0)
	{
		debugPrintf("Could not set non blocking flag %d", errno);
	}
	SetState(ConnState::connected);
	return ERR_OK;
}


// Static functions
/*static*/ Connection *Connection::Allocate()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->state == ConnState::free)
		{
			return connectionList[i];
		}
	}
	return nullptr;
}

/*static*/ void Connection::Init()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i] = new Connection((uint8_t)i);
	}
#ifdef EXTENDED_LISTEN
	for(size_t i = 0; i < MaxPublicConnections; i++)
	{
		publicConnections[i] = connectionList[i];
		publicConnections[i]->number = (uint8_t)i;
	}
#endif
}

/*static*/ uint16_t Connection::CountConnectionsOnPort(uint16_t port)
{
	uint16_t count = 0;
	//debugPrintf("Count conns port %x\n", port);
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->localPort == port)
		{
			const ConnState state = connectionList[i]->state;
			if (state == ConnState::connected || state == ConnState::otherEndClosed || state == ConnState::closePending)
			{
				++count;
			}
		}
	}
	//debugPrintf("Count conns %d\n", count);
	return count;
}

/*static*/ void ICACHE_RAM_ATTR Connection::PollOne()
{
	//Listener::Poll();
	//debugPrintf("Pollone %d\n", nextConnectionToPoll);
	uint32_t next = nextConnectionToPoll;
	do {
		if (++next == MaxConnections)
		{
			Listener::Poll();
			next = 0;
		}
		if (connectionList[next]->state != ConnState::free)
		{
			connectionList[next]->Poll();
			nextConnectionToPoll = next;
		}
	} while (next != nextConnectionToPoll);
}

/*static*/ void Connection::TerminateAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i]->Terminate(true);
	}
}

static uint16_t prevOpen = 0;
static uint16_t prevClosed = 0;

/*static*/ void Connection::GetSummarySocketStatus(uint16_t& connectedSockets, uint16_t& otherEndClosedSockets)
{
	connectedSockets = 0;
	otherEndClosedSockets = 0;
#ifdef EXTENDED_LISTEN
	for (size_t i = 0; i < MaxPublicConnections; ++i)
#else
	for (size_t i = 0; i < MaxConnections; ++i)
#endif
	{
		if (Connection::Get(i).GetState() == ConnState::connected)
		{
			connectedSockets |= (1 << i);
		}
		else if (Connection::Get(i).GetState() == ConnState::otherEndClosed)
		{
			otherEndClosedSockets |= (1 << i);
		}
	}
	if (connectedSockets != prevOpen || otherEndClosedSockets != prevClosed) 
	{
		//debugPrintf("connected %x closed %x\n", connectedSockets, otherEndClosedSockets);
		prevOpen = connectedSockets;
		prevClosed = otherEndClosedSockets;
	}
}

/*static*/ void Connection::ReportConnections()
{
	ets_printf("Conns");
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		ets_printf("%c %u:", (i == 0) ? ':' : ',', i);
		connectionList[i]->Report();
	}
	ets_printf("\n");
}

// Static data
Connection *Connection::connectionList[MaxConnections] = { 0 };
#ifdef EXTENDED_LISTEN
Connection *Connection::publicConnections[MaxPublicConnections] = { 0 };
#endif
size_t Connection::nextConnectionToPoll = 0;

#else
#include "Connection.h"
#include "algorithm"			// for std::min
#include "Arduino.h"			// for millis
#include "Config.h"

const uint32_t MaxWriteTime = 2000;		// how long we wait for a write operation to complete before it is cancelled
const uint32_t MaxAckTime = 4000;		// how long we wait for a connection to acknowledge the remaining data before it is closed

// C interface functions
extern "C"
{
	#include "lwip/init.h"				// for version info
	#include "lwip/tcp.h"

	static void conn_err(void *arg, err_t err)
	{
		if (arg != nullptr)
		{
			((Connection*)arg)->ConnError(err);
		}
	}

	static err_t conn_recv(void *arg, tcp_pcb *pcb, pbuf *p, err_t err)
	{
		if (arg != nullptr)
		{
			return ((Connection*)arg)->ConnRecv(p, err);
		}
		return ERR_ABRT;
	}

	static err_t conn_sent(void *arg, tcp_pcb *pcb, u16_t len)
	{
		if (arg != nullptr)
		{
			return ((Connection*)arg)->ConnSent(len);
		}
		return ERR_ABRT;
	}
}

// Public interface
Connection::Connection(uint8_t num) :
#ifdef EXTENDED_LISTEN
 number(0xff),
#else
 number(num),
#endif
 state(ConnState::free), localPort(0), remotePort(0), remoteIp(0), writeTimer(0), closeTimer(0),
	  unAcked(0), readIndex(0), alreadyRead(0), ownPcb(nullptr), pb(nullptr)
{
}

void Connection::GetStatus(ConnStatusResponse& resp) const
{
	resp.socketNumber = number;
	resp.state = state;
	resp.bytesAvailable = CanRead();
	resp.writeBufferSpace = CanWrite();
	resp.localPort = localPort;
	resp.remotePort = remotePort;
	resp.remoteIp = remoteIp;
}

// Close the connection gracefully
void Connection::Close()
{
	switch(state)
	{
	case ConnState::connected:						// both ends are still connected
		if (unAcked != 0)
		{
			closeTimer = millis();
			SetState(ConnState::closePending);		// wait for the remaining data to be sent before closing
			break;
		}
		// no break
	case ConnState::otherEndClosed:					// the other end has already closed the connection
	case ConnState::closeReady:						// the other end has closed and we were already closePending
	default:										// should not happen
		if (ownPcb != nullptr)
		{
			tcp_recv(ownPcb, nullptr);
			tcp_sent(ownPcb, nullptr);
			tcp_err(ownPcb, nullptr);
			tcp_close(ownPcb);
			ownPcb = nullptr;
		}
		unAcked = 0;
		FreePbuf();
		SetState(ConnState::free);
		break;

	case ConnState::closePending:					// we already asked to close
		// Should not happen, but if it does just let the close proceed when sending is complete or timeout
		break;
	}
}

// Terminate the connection.
// If 'external' is true then the Duet main processor has requested termination, so we free up the connection.
// Otherwise it has failed because of an internal error, and we set the state to 'aborted'. The Duet main processor will see this and send a termination request,
// which will free it up.
void Connection::Terminate(bool external)
{
	if (ownPcb != nullptr)
	{
		tcp_recv(ownPcb, nullptr);
		tcp_sent(ownPcb, nullptr);
		tcp_err(ownPcb, nullptr);
		tcp_abort(ownPcb);
		ownPcb = nullptr;
	}
	unAcked = 0;
	FreePbuf();
	SetState((external) ? ConnState::free : ConnState::aborted);
}

// Perform housekeeping tasks
void Connection::Poll()
{
#ifdef EXTENDED_LISTEN
	if (state == ConnState::free)
		return;
	// Are we waiting to be made public?
	if (number == 0xff)
		MakePublic();
#endif
	if (state == ConnState::connected)
	{
		// Are we still waiting for data to be written?
		if (writeTimer > 0 && millis() - writeTimer >= MaxWriteTime)
		{
			// Terminate it
			Terminate(false);
		}
	}
	else if (state == ConnState::closeReady)
	{
		// Deferred close, possibly outside the ISR
		Close();
	}
	else if (state == ConnState::closePending)
	{
		// We're about to close this connection and we're still waiting for the remaining data to be acknowledged
		if (unAcked == 0)
		{
			// All data has been received, close this connection next time
			SetState(ConnState::closeReady);
		}
		else if (millis() - closeTimer >= MaxAckTime)
		{
			// The acknowledgement timer has expired, abort this connection
			Terminate(false);
		}
	}
}

// Write data to the connection. The amount of data may be zero.
// A note about writing:
// - LWIP is compiled with option LWIP_NETIF_TX_SINGLE_PBUF set. A comment says this is mandatory for the ESP8266.
// - A side effect of this is that when we call tcp_write, the data is always copied even if we don't set the TCP_WRITE_FLAG_COPY flag.
// - The PBUFs used to copy the outgoing data into are always large enough to accommodate the MSS. The total allocation size per PBUF is 1560 bytes.
// - Sending a full 2K of data may require 2 of these PBUFs to be allocated.
// - Due to memory fragmentation and other pending packets, this allocation is sometimes fails if we are serving more than 2 files at a time.
// - The result returned by tcp_sndbuf doesn't take account of the possibility that this allocation may fail.
// - When it receives a write request from the Duet main processor, our socket server has to say how much data it can accept before accepting it.
// - So in version 1.21 it sometimes happened that we accept some data based on the amount that tcp_sndbuf say we can, but we can't actually send it.
// - We then terminate the connection, and the client request fails.
// To mitigate this we could:
// - Have one overflow write buffer, shared between all connections
// - Only accept write data from the Duet main processor if the overflow buffer is free
// - If after accepting data from the Duet main processor we find that we can't send it, we send some of it if we can and store the rest in the overflow buffer
// - Then we push any pending data that we already have, and in Poll() we try to send the data in overflow buffer
// - When the overflow buffer is empty again, we can start accepting write data from the Duet main processor again.
// A further mitigation would be to restrict the amount of data we accept so some amount that will fit in the MSS, then tcp_write will need to allocate at most one PBUF.
// However, another reason why tcp_write can fail is because MEMP_NUM_TCP_SEG is set too low in Lwip. It now appears that this is the maoin cause of files tcp_write
// call in version 1.21. So I have increased it from 10 to 16, which seems to have fixed the problem..
size_t Connection::Write(const uint8_t *data, size_t length, bool doPush, bool closeAfterSending)
{
	if (state != ConnState::connected)
	{
		return 0;
	}

	// Try to send all the data
	const bool push = doPush || closeAfterSending;
	err_t result = tcp_write(ownPcb, data, length, push ? TCP_WRITE_FLAG_COPY : TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
	if (result != ERR_OK)
	{
		// We failed to write the data. See above for possible mitigations. For now we just terminate the connection.
		debugPrintfAlways("Write fail len=%u err=%d\n", length, (int)result);
		Terminate(false);		// chrishamm: Not sure if this helps with LwIP v1.4.3 but it is mandatory for proper error handling with LwIP 2.0.3
		return 0;
	}

	// Data was successfully written
	writeTimer = 0;
	unAcked += length;

	// See if we need to push the remaining data
	if (push || tcp_sndbuf(ownPcb) <= TCP_SNDLOWAT)
	{
		tcp_output(ownPcb);
	}

	// Close the connection again when we're done
	if (closeAfterSending)
	{
		closeTimer = millis();
		SetState(ConnState::closePending);
	}
	return length;
}

size_t Connection::CanWrite() const
{
	// Return the amount of free space in the write buffer
	// Note: we cannot necessarily write this amount, because it depends on memory allocations being successful.
	return (state == ConnState::connected) ? tcp_sndbuf(ownPcb) : 0;
}

size_t Connection::Read(uint8_t *data, size_t length)
{
	size_t lengthRead = 0;
	if (pb != nullptr && length != 0 && (state == ConnState::connected || state == ConnState::otherEndClosed))
	{
		do
		{
			const size_t toRead = std::min<size_t>(pb->len - readIndex, length);
			memcpy(data + lengthRead, (uint8_t *)pb->payload + readIndex, toRead);
			lengthRead += toRead;
			readIndex += toRead;
			length -= toRead;
			if (readIndex != pb->len)
			{
				break;
			}
			pbuf * const currentPb = pb;
			pb = pb->next;
			currentPb->next = nullptr;
			pbuf_free(currentPb);
			readIndex = 0;
		} while (pb != nullptr && length != 0);

		alreadyRead += lengthRead;
		if (pb == nullptr || alreadyRead >= TCP_MSS)
		{
			tcp_recved(ownPcb, alreadyRead);
			alreadyRead = 0;
		}
	}
	return lengthRead;
}

size_t Connection::CanRead() const
{
	return ((state == ConnState::connected || state == ConnState::otherEndClosed) && pb != nullptr)
			? pb->tot_len - readIndex
				: 0;
}

void Connection::Report()
{
	// The following must be kept in the same order as the declarations in class ConnState
	static const char* const connStateText[] =
	{
		"free",
		"connecting",			// socket is trying to connect
		"connected",			// socket is connected
		"remoteClosed",			// the other end has closed the connection

		"aborted",				// an error has occurred
		"closePending",			// close this socket when sending is complete
		"closeReady"			// about to be closed
	};

	const unsigned int st = (int)state;
	ets_printf("%s", (st < ARRAY_SIZE(connStateText)) ? connStateText[st]: "unknown");
	if (state != ConnState::free)
	{
		ets_printf(" %u, %u, %u.%u.%u.%u", localPort, remotePort, remoteIp & 255, (remoteIp >> 8) & 255, (remoteIp >> 16) & 255, (remoteIp >> 24) & 255);
	}
}

#ifdef EXTENDED_LISTEN
void Connection::MakePublic()
{
	// make this connection public if we can
	for(uint32_t i = 0; i < MaxPublicConnections; i++)
	{
		// If one of our public connections is not in use, use that slot
		if (publicConnections[i]->state == ConnState::free)
		{
			// Found a public socket that is not in use
			publicConnections[i]->number = 0xff;
			number = i;
			publicConnections[i] = this;
			return;
		}
	}
}
#endif

// Callback functions
int Connection::Accept(tcp_pcb *pcb)
{
	ownPcb = pcb;
	tcp_arg(pcb, this);				// tell LWIP that this is the structure we wish to be passed for our callbacks
	tcp_recv(pcb, conn_recv);		// tell LWIP that we wish to be informed of incoming data by a call to the conn_recv() function
	tcp_sent(pcb, conn_sent);
	tcp_err(pcb, conn_err);
	SetState(ConnState::connected);
	localPort = pcb->local_port;
	remotePort = pcb->remote_port;
	remoteIp = pcb->remote_ip.addr;
	writeTimer = closeTimer = 0;
	unAcked = readIndex = alreadyRead = 0;
	return ERR_OK;
}

void Connection::ConnError(int err)
{
	if (ownPcb != nullptr)
	{
		tcp_sent(ownPcb, nullptr);
		tcp_recv(ownPcb, nullptr);
		tcp_err(ownPcb, nullptr);
		ownPcb = nullptr;
	}
	FreePbuf();
	SetState(ConnState::aborted);
}

int Connection::ConnRecv(pbuf *p, int err)
{
	if (p == nullptr)
	{
		// The other end has closed the connection
		if (state == ConnState::connected)
		{
			SetState(ConnState::otherEndClosed);
		}
		else if (state == ConnState::closePending)
		{
			// We could perhaps call tcp_close here, but perhaps better to do it outside the callback
			state = ConnState::closeReady;
		}
	}
	else if (pb != nullptr)
	{
		pbuf_cat(pb, p);
	}
	else
	{
		pb = p;
		readIndex = alreadyRead = 0;
	}
	//debugPrint("Packet rcvd\n");
	return ERR_OK;
}

// This is called when sent data has been acknowledged
int Connection::ConnSent(uint16_t len)
{
	if (len <= unAcked)
	{
		unAcked -= len;
	}
	else
	{
		// Something is wrong, more data has been acknowledged than has been sent (hopefully this will never occur)
		unAcked = 0;
	}
	return ERR_OK;
}

void Connection::FreePbuf()
{
	if (pb != nullptr)
	{
		pbuf_free(pb);
		pb = nullptr;
	}
}

// Static functions
/*static*/ Connection *Connection::Allocate()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->state == ConnState::free)
		{
			return connectionList[i];
		}
	}
	return nullptr;
}

/*static*/ void Connection::Init()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i] = new Connection((uint8_t)i);
	}
#ifdef EXTENDED_LISTEN
	for(size_t i = 0; i < MaxPublicConnections; i++)
	{
		publicConnections[i] = connectionList[i];
		publicConnections[i]->number = (uint8_t)i;
	}
#endif
}

/*static*/ uint16_t Connection::CountConnectionsOnPort(uint16_t port)
{
	uint16_t count = 0;
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		if (connectionList[i]->localPort == port)
		{
			const ConnState state = connectionList[i]->state;
			if (state == ConnState::connected || state == ConnState::otherEndClosed || state == ConnState::closePending)
			{
				++count;
			}
		}
	}
	return count;
}

/*static*/ void Connection::PollOne()
{
	connectionList[nextConnectionToPoll]->Poll();
	++nextConnectionToPoll;
	if (nextConnectionToPoll == MaxConnections)
	{
		nextConnectionToPoll = 0;
	}
}

/*static*/ void Connection::TerminateAll()
{
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		connectionList[i]->Terminate(true);
	}
}

/*static*/ void Connection::GetSummarySocketStatus(uint16_t& connectedSockets, uint16_t& otherEndClosedSockets)
{
	connectedSockets = 0;
	otherEndClosedSockets = 0;
#ifdef EXTENDED_LISTEN
	for (size_t i = 0; i < MaxPublicConnections; ++i)
#else
	for (size_t i = 0; i < MaxConnections; ++i)
#endif
	{
		if (Connection::Get(i).GetState() == ConnState::connected)
		{
			connectedSockets |= (1 << i);
		}
		else if (Connection::Get(i).GetState() == ConnState::otherEndClosed)
		{
			otherEndClosedSockets |= (1 << i);
		}
	}
}

/*static*/ void Connection::ReportConnections()
{
	ets_printf("Conns");
	for (size_t i = 0; i < MaxConnections; ++i)
	{
		ets_printf("%c %u:", (i == 0) ? ':' : ',', i);
		connectionList[i]->Report();
	}
	ets_printf("\n");
}

// Static data
Connection *Connection::connectionList[MaxConnections] = { 0 };
#ifdef EXTENDED_LISTEN
Connection *Connection::publicConnections[MaxPublicConnections] = { 0 };
#endif
size_t Connection::nextConnectionToPoll = 0;
#endif
// End

#include "Net/SwcRepConnection.h"

#include "HAL/RunnableThread.h"

#if !PLATFORM_UNIX
#error "The game server connection uses the BSD sockets of a Unix platform."
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
THIRD_PARTY_INCLUDES_END
#undef UI

namespace
{
using FEventLevel = swc::net::RepClient::Event::Level;
using FClock = swc::net::Clock;

/** The cipher suite of the retail client and the backend (protocol notes 3.1). */
const char* const CipherSuite = "ECDHE-RSA-AES256-GCM-SHA384";
/** The DTLS handshake must end in this time. */
constexpr int32 HandshakeLimitMs = 10000;
/** The link MTU of loopback for DTLS: a carrier datagram (1,038 bytes) and the record overhead fit. */
constexpr long LinkMtu = 1500;
/** The poll time of the thread and the tick interval of the session (the backend ticks each 10 ms). */
constexpr int PollMs = 5;
constexpr auto TickInterval = std::chrono::milliseconds(10);
/** After the end of a session the thread sends its last datagrams (the carrier disconnect) and then ends. */
constexpr auto LingerTime = std::chrono::milliseconds(100);

FString OpenSslError()
{
	char Text[256] = {};
	const unsigned long Code = ERR_get_error();
	if (Code == 0)
	{
		return TEXT("no OpenSSL error");
	}
	ERR_error_string_n(Code, Text, sizeof(Text));
	ERR_clear_error();
	return UTF8_TO_TCHAR(Text);
}

/** The objects of one DTLS connection; the destructor frees them (no close notice). */
struct FDtls
{
	int Socket = -1;
	SSL_CTX* Context = nullptr;
	SSL* Session = nullptr;

	~FDtls()
	{
		if (Session != nullptr)
		{
			SSL_free(Session);
		}
		if (Context != nullptr)
		{
			SSL_CTX_free(Context);
		}
		if (Socket >= 0)
		{
			::close(Socket);
		}
	}
};
} // namespace

FSwcRepConnection::FSwcRepConnection(FOptions InOptions)
	: Options(MoveTemp(InOptions))
{
}

FSwcRepConnection::~FSwcRepConnection()
{
	if (Thread != nullptr)
	{
		Stop();
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

bool FSwcRepConnection::Launch()
{
	Thread = FRunnableThread::Create(this, TEXT("SwcRepConnection"), 0, TPri_AboveNormal);
	return Thread != nullptr;
}

void FSwcRepConnection::Leave(const FString& Cause)
{
	FScopeLock Guard(&Lock);
	if (LeaveCause.IsEmpty())
	{
		LeaveCause = Cause;
	}
}

void FSwcRepConnection::Stop()
{
	Leave(TEXT("the client stops"));
	bStopRequested = true;
}

std::vector<swc::net::RepClient::Event> FSwcRepConnection::TakeEvents()
{
	FScopeLock Guard(&Lock);
	std::vector<swc::net::RepClient::Event> Out;
	Out.swap(Events);
	return Out;
}

void FSwcRepConnection::SetMotion(const std::optional<swc::net::RepClient::Motion>& InMotion)
{
	FScopeLock Guard(&Lock);
	Motion = InMotion;
}

void FSwcRepConnection::QueueContacts(const std::vector<swc::net::MeleeContactItem>& InContacts)
{
	FScopeLock Guard(&Lock);
	Contacts.insert(Contacts.end(), InContacts.begin(), InContacts.end());
}

void FSwcRepConnection::RequestRespawn(bool bRandom, const FString& HomeId)
{
	FScopeLock Guard(&Lock);
	const std::string Id = TCHAR_TO_UTF8(*HomeId);
	Calls.push_back([bRandom, Id](swc::net::RepClient& Client) { Client.request_respawn(bRandom, Id); });
}

void FSwcRepConnection::TakeAllItems()
{
	FScopeLock Guard(&Lock);
	Calls.push_back([](swc::net::RepClient& Client) { Client.take_all_items(); });
}

void FSwcRepConnection::TakeItem(const std::array<uint8, 16>& Instance, uint32 Quantity)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([Instance, Quantity](swc::net::RepClient& Client) { Client.take_item(Instance, Quantity); });
}

void FSwcRepConnection::EquipItem(int32 BagSlot, uint32 PaperdollSlot, uint32 Quantity, const swc::net::ItemInstanceId& Instance)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([=](swc::net::RepClient& Client) { Client.equip_item(BagSlot, PaperdollSlot, Quantity, Instance); });
}

void FSwcRepConnection::UnequipItem(uint32 PaperdollSlot, int32 BagSlot, uint32 Quantity)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([=](swc::net::RepClient& Client) { Client.unequip_item(PaperdollSlot, BagSlot, Quantity); });
}

void FSwcRepConnection::SwapItems(uint32 SlotTo, uint32 SlotFrom, const swc::net::ItemInstanceId& InstanceTo, const swc::net::ItemInstanceId& InstanceFrom)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([=](swc::net::RepClient& Client) { Client.swap_items(SlotTo, SlotFrom, InstanceTo, InstanceFrom); });
}

void FSwcRepConnection::UseItem(uint32 BagSlot)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([=](swc::net::RepClient& Client) { Client.use_item(BagSlot); });
}

void FSwcRepConnection::DropItems(const std::vector<swc::net::DropItemEntry>& Entries)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([Entries](swc::net::RepClient& Client) { Client.drop_items(Entries); });
}

void FSwcRepConnection::DropPaperdollItem(uint32 PaperdollSlot, uint32 Quantity)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([=](swc::net::RepClient& Client) { Client.drop_paperdoll_item(PaperdollSlot, Quantity); });
}

void FSwcRepConnection::SetInteraction(const std::optional<swc::net::InteractionRequest>& Request)
{
	FScopeLock Guard(&Lock);
	Calls.push_back([Request](swc::net::RepClient& Client) { Client.set_interaction(Request); });
}

void FSwcRepConnection::CancelInteraction()
{
	FScopeLock Guard(&Lock);
	Calls.push_back([](swc::net::RepClient& Client) { Client.cancel_interaction(); });
}

std::vector<swc::net::RepClient::OtherUpdate> FSwcRepConnection::TakeOtherUpdates()
{
	FScopeLock Guard(&Lock);
	std::vector<swc::net::RepClient::OtherUpdate> Out;
	Out.swap(OtherUpdates);
	return Out;
}

FSwcRepConnection::FSnapshot FSwcRepConnection::GetSnapshot() const
{
	FScopeLock Guard(&Lock);
	return Snapshot;
}

void FSwcRepConnection::Post(FEventLevel Level, const FString& Text)
{
	FScopeLock Guard(&Lock);
	Events.push_back({Level, std::string(TCHAR_TO_UTF8(*Text))});
}

void FSwcRepConnection::Publish(const swc::net::RepClient& Client, bool bFinished)
{
	FScopeLock Guard(&Lock);
	Snapshot.Phase = Client.phase();
	Snapshot.Own = Client.own();
	Snapshot.Counts = Client.counts();
	Snapshot.OtherEntities = static_cast<int32>(Client.others().size());
	Snapshot.Others.Reset();
	Snapshot.Objects.Reset();
	for (const auto& [Interest, Other] : Client.others())
	{
		if (Other.hasPosition)
		{
			Snapshot.Others.Emplace(Interest, Other.position);
		}
		if (Other.hasPlace)
		{
			Snapshot.Objects.Emplace(Interest, Other);
		}
	}
	Snapshot.bMovementStale = Client.movement_stale();
	Snapshot.Reported = Client.reported();
	Snapshot.bServerClock = Client.has_server_clock();
	Snapshot.ServerOffsetNs = Client.server_clock_offset_ns();
	Snapshot.EndCause = UTF8_TO_TCHAR(Client.end_cause().c_str());
	Snapshot.bFinished = bFinished;
}

uint32 FSwcRepConnection::Run()
{
	swc::net::RepClient Client(Options.Session);
	const auto Finish = [this, &Client](const FString& Cause)
	{
		Post(FEventLevel::error, Cause);
		FScopeLock Guard(&Lock);
		Snapshot.EndCause = Cause;
		Snapshot.Phase = Client.phase();
		Snapshot.bFinished = true;
		return 1u;
	};

	// The socket: loopback only (the rule of the project), connected to the game server.
	sockaddr_in Server = {};
	Server.sin_family = AF_INET;
	Server.sin_port = htons(static_cast<uint16>(Options.Port));
	if (::inet_pton(AF_INET, TCHAR_TO_UTF8(*Options.Host), &Server.sin_addr) != 1 || (ntohl(Server.sin_addr.s_addr) >> 24) != 127)
	{
		return Finish(FString::Printf(TEXT("dtls: the game server address %s is not a loopback address"), *Options.Host));
	}
	FDtls Dtls;
	Dtls.Socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in Local = {};
	Local.sin_family = AF_INET;
	Local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (Dtls.Socket < 0 || ::bind(Dtls.Socket, reinterpret_cast<sockaddr*>(&Local), sizeof(Local)) != 0
		|| ::connect(Dtls.Socket, reinterpret_cast<sockaddr*>(&Server), sizeof(Server)) != 0
		|| ::fcntl(Dtls.Socket, F_SETFL, ::fcntl(Dtls.Socket, F_GETFL, 0) | O_NONBLOCK) != 0)
	{
		return Finish(FString::Printf(TEXT("dtls: no UDP socket to %s:%d"), *Options.Host, Options.Port));
	}
	socklen_t LocalSize = sizeof(Local);
	::getsockname(Dtls.Socket, reinterpret_cast<sockaddr*>(&Local), &LocalSize);

	// DTLS 1.2 with the cipher of the backend; the client trusts only the certificate of the backend.
	Dtls.Context = SSL_CTX_new(DTLS_client_method());
	if (Dtls.Context == nullptr || SSL_CTX_set_min_proto_version(Dtls.Context, DTLS1_2_VERSION) != 1
		|| SSL_CTX_set_max_proto_version(Dtls.Context, DTLS1_2_VERSION) != 1 || SSL_CTX_set_cipher_list(Dtls.Context, CipherSuite) != 1)
	{
		return Finish(TEXT("dtls: no DTLS 1.2 context: ") + OpenSslError());
	}
	{
		const FTCHARToUTF8 Pem(*Options.CertificatePem);
		BIO* Memory = BIO_new_mem_buf(Pem.Get(), Pem.Length());
		X509* Certificate = Memory != nullptr ? PEM_read_bio_X509(Memory, nullptr, nullptr, nullptr) : nullptr;
		const bool bTrusted = Certificate != nullptr && X509_STORE_add_cert(SSL_CTX_get_cert_store(Dtls.Context), Certificate) == 1;
		X509_free(Certificate);
		BIO_free(Memory);
		if (!bTrusted)
		{
			return Finish(TEXT("dtls: the certificate of the backend did not load: ") + OpenSslError());
		}
	}
	SSL_CTX_set_verify(Dtls.Context, SSL_VERIFY_PEER, nullptr);
	Dtls.Session = SSL_new(Dtls.Context);
	BIO* Datagrams = BIO_new_dgram(Dtls.Socket, BIO_NOCLOSE);
	if (Dtls.Session == nullptr || Datagrams == nullptr)
	{
		return Finish(TEXT("dtls: no DTLS session: ") + OpenSslError());
	}
	BIO_ctrl(Datagrams, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &Server);
	SSL_set_bio(Dtls.Session, Datagrams, Datagrams);
	SSL_set_options(Dtls.Session, SSL_OP_NO_QUERY_MTU);
	DTLS_set_link_mtu(Dtls.Session, LinkMtu);

	const auto Wait = [&Dtls](int TimeoutMs)
	{
		pollfd Descriptor{Dtls.Socket, POLLIN, 0};
		return ::poll(&Descriptor, 1, TimeoutMs) > 0 && (Descriptor.revents & POLLIN) != 0;
	};
	const FClock::time_point HandshakeStart = FClock::now();
	for (;;)
	{
		const int Result = SSL_connect(Dtls.Session);
		if (Result == 1)
		{
			break;
		}
		const int Error = SSL_get_error(Dtls.Session, Result);
		if (Error != SSL_ERROR_WANT_READ && Error != SSL_ERROR_WANT_WRITE)
		{
			return Finish(FString::Printf(TEXT("dtls: the handshake failed (%d): %s"), Error, *OpenSslError()));
		}
		if (bStopRequested || FClock::now() - HandshakeStart > std::chrono::milliseconds(HandshakeLimitMs))
		{
			return Finish(TEXT("dtls: no handshake in 10 s"));
		}
		timeval Timeout = {};
		const bool bTimer = DTLSv1_get_timeout(Dtls.Session, &Timeout) == 1;
		const int WaitMs = bTimer ? FMath::Clamp(static_cast<int>(Timeout.tv_sec * 1000 + Timeout.tv_usec / 1000), 1, 100) : 100;
		if (!Wait(WaitMs))
		{
			DTLSv1_handle_timeout(Dtls.Session);
		}
	}
	X509* Peer = SSL_get_peer_certificate(Dtls.Session);
	char Subject[256] = {};
	if (Peer != nullptr)
	{
		X509_NAME_oneline(X509_get_subject_name(Peer), Subject, sizeof(Subject));
		X509_free(Peer);
	}
	Post(FEventLevel::info, FString::Printf(TEXT("dtls: connected to %s:%d from port %u, %s, cipher %s, certificate %s"), *Options.Host,
		Options.Port, ntohs(Local.sin_port), UTF8_TO_TCHAR(SSL_get_version(Dtls.Session)), UTF8_TO_TCHAR(SSL_get_cipher(Dtls.Session)),
		UTF8_TO_TCHAR(Subject)));

	Client.start(FClock::now());
	FClock::time_point NextTick = FClock::now();
	TOptional<FClock::time_point> EndedAt;
	FString TransportEnd;
	uint8 Buffer[16384];
	while (true)
	{
		Wait(PollMs);
		FClock::time_point Now = FClock::now();
		// Each DTLS record is one carrier datagram of the server.
		for (;;)
		{
			const int Read = SSL_read(Dtls.Session, Buffer, sizeof(Buffer));
			if (Read > 0)
			{
				Client.receive(std::span<const std::uint8_t>(Buffer, static_cast<std::size_t>(Read)), Now);
				continue;
			}
			const int Error = SSL_get_error(Dtls.Session, Read);
			if (Error == SSL_ERROR_ZERO_RETURN)
			{
				TransportEnd = TEXT("dtls: the backend sent a close notice");
			}
			else if (Error != SSL_ERROR_WANT_READ && Error != SSL_ERROR_WANT_WRITE)
			{
				TransportEnd = FString::Printf(TEXT("dtls: read failed (%d): %s"), Error, *OpenSslError());
			}
			break;
		}
		if (!TransportEnd.IsEmpty() && !EndedAt.IsSet())
		{
			Client.leave(TCHAR_TO_UTF8(*TransportEnd));
		}
		{
			FString Cause;
			{
				FScopeLock Guard(&Lock);
				Cause = LeaveCause;
			}
			if (!Cause.IsEmpty() && Client.phase() != swc::net::RepClient::Phase::ended)
			{
				Client.leave(TCHAR_TO_UTF8(*Cause));
			}
		}
		{
			FScopeLock Guard(&Lock);
			Client.set_motion(Motion);
			if (!Contacts.empty())
			{
				Client.queue_contacts(Contacts);
				Contacts.clear();
			}
			for (const std::function<void(swc::net::RepClient&)>& Call : Calls)
			{
				Call(Client);
			}
			Calls.clear();
		}
		if (Now >= NextTick)
		{
			NextTick = Now + TickInterval;
			for (const swc::net::Bytes& Datagram : Client.tick(Now))
			{
				if (TransportEnd.IsEmpty() && SSL_write(Dtls.Session, Datagram.data(), static_cast<int>(Datagram.size())) <= 0)
				{
					TransportEnd = TEXT("dtls: write failed: ") + OpenSslError();
				}
			}
		}
		{
			FScopeLock Guard(&Lock);
			for (swc::net::RepClient::Event& Event : Client.take_events())
			{
				Events.push_back(std::move(Event));
			}
			for (swc::net::RepClient::OtherUpdate& Update : Client.take_other_updates())
			{
				OtherUpdates.push_back(std::move(Update));
			}
			if (OtherUpdates.size() > swc::net::RepClient::kOtherUpdateLimit)
			{
				// The game thread takes them each frame; a long stop of the game thread (a level load) drops the oldest half.
				OtherUpdates.erase(OtherUpdates.begin(), OtherUpdates.begin() + swc::net::RepClient::kOtherUpdateLimit / 2);
			}
		}
		const bool bEnded = Client.phase() == swc::net::RepClient::Phase::ended;
		if (bEnded && !EndedAt.IsSet())
		{
			EndedAt = Now;
		}
		Publish(Client, false);
		if (EndedAt.IsSet() && Now - EndedAt.GetValue() >= LingerTime)
		{
			break;
		}
	}
	const swc::net::Carrier::Counts& Counts = Client.carrier().counts();
	Post(FEventLevel::info, FString::Printf(TEXT("dtls: session end; %llu datagrams in (largest %llu bytes), %llu out (%llu bytes), %llu resends"),
		static_cast<unsigned long long>(Counts.datagramsIn), static_cast<unsigned long long>(Counts.largestDatagramIn),
		static_cast<unsigned long long>(Counts.datagramsOut), static_cast<unsigned long long>(Counts.bytesOut),
		static_cast<unsigned long long>(Counts.resends)));
	Publish(Client, true);
	return 0;
}

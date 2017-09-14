#ifdef WIN32
#include <Winsock2.h>
#endif

#include <thread>
#include <iostream>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <vector>
#include <memory>
#include <string>
#include <regex>
#include <random>

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/thread.h>


namespace
{
	struct NetAddr final
	{
		std::string host;
		uint16_t port;

		static NetAddr from(const std::string& s)
		{
			static const std::regex NetAddRx("(.+):(\\d+)");

			std::smatch match_res;
			if (!std::regex_match(s, match_res, NetAddRx))
				throw std::runtime_error("Invalid address format");

			std::string host = match_res[1];
			uint16_t port = std::stoi(match_res[2]);
			return { host, port };
		}
	};
	using NetAddrs = std::vector<NetAddr>;


	void initEventLib()
	{
#ifdef WIN32
		WSADATA WSAData;
		WSAStartup(0x101, &WSAData);
		evthread_use_windows_threads();
#else
		evthread_use_pthreads();
#endif
	}


	using EventBaseUPtr = std::unique_ptr<event_base, decltype(&event_base_free)>;
	using EvhttpConnectionUPtr = std::unique_ptr<evhttp_connection, decltype(&evhttp_connection_free)>;
	using EvhttpConnections = std::vector<EvhttpConnectionUPtr>;


	EventBaseUPtr makeEventBase()
	{
		event_config *cfg = event_config_new();

#ifdef WIN32
		//event_config_set_flag(cfg, EVENT_BASE_FLAG_STARTUP_IOCP);
		event_config_set_num_cpus_hint(cfg, 4);
#endif

		auto base = EventBaseUPtr(event_base_new_with_config(cfg), &event_base_free);
		event_config_free(cfg);
		return base;
	}


	EvhttpConnectionUPtr newEvhttpConnection(event_base* base, evdns_base* dnsbase, const char* address, uint16_t port)
	{
		return EvhttpConnectionUPtr(
			evhttp_connection_base_new(base, dnsbase, address, port),
			&evhttp_connection_free);
	}


	EvhttpConnections makeConnections(event_base* base, const NetAddrs& netAddrs, size_t connCount)
	{
		assert(!netAddrs.empty());

		std::random_device rd;
		std::mt19937 gen;
		std::uniform_int_distribution<size_t> dis(0, netAddrs.size() - 1);

		EvhttpConnections ret;
		for (size_t i = 0; i < connCount; ++i)
		{
			auto& netAddr = netAddrs.size() > 1 ? netAddrs[dis(gen)] : netAddrs.front();
			ret.push_back(newEvhttpConnection(base, nullptr, netAddr.host.data(), netAddr.port));
		}

		return ret;
	}


	std::atomic<int32_t> reqCounter(0);

	extern void sendReq(evhttp_connection* conn);

	void reqCallback(evhttp_request* req, void* arg)
	{
		if (req != nullptr)
		{
			auto code = evhttp_request_get_response_code(req);
			if (code == 200)
				++reqCounter;
			else
				std::cout << "Invalid response_code: " << code << std::endl;
		}

		auto evcon = reinterpret_cast<evhttp_connection*>(arg);
		sendReq(evcon);
	}


	void recivedChunck(evhttp_request* req, void* arg)
	{
	}

	void reqFailed(evhttp_request_error error, void *arg)
	{
		std::cout << __FUNCTION__ << ": " << error << std::endl;
	}

	void sendReq(evhttp_connection* evcon)
	{
		auto req = evhttp_request_new(&reqCallback, evcon);
		evhttp_request_set_chunked_cb(req, &recivedChunck);

		auto headers = evhttp_request_get_output_headers(req);
		evhttp_add_header(headers, "Host", "172.17.1.250");
		evhttp_add_header(headers, "Connection", "keep-alive");

		evhttp_make_request(evcon, req, EVHTTP_REQ_GET, "/hello");
	}

	void statThread()
	{
		auto start = std::chrono::steady_clock::now();
		int64_t startReqCount = reqCounter;

		while (true)
		{
			using ms = std::chrono::milliseconds;

			std::this_thread::sleep_for(ms(2500));
			int64_t stopReqCount = reqCounter;
			auto stop = std::chrono::steady_clock::now();

			double duration_in_sec = std::chrono::duration_cast<ms>(stop - start).count() / 1000.0;
			double rps = static_cast<double>(stopReqCount - startReqCount) / duration_in_sec;
			std::cout << "RPS=" << rps << std::endl;

			start = stop;
			startReqCount = stopReqCount;
		}
	}

	struct ProgramOpts
	{
		unsigned connCount = 0;
		NetAddrs addrs;
	};


	void usage(const char* program)
	{
		std::cout << program
			<< " <connCount>"
			<< " <host:port> <host:port> ..."
			<< std::endl;
		exit(EXIT_FAILURE);
	}


	ProgramOpts parseArgs(int argc, char* argv[])
	{
		if (argc < 3)
			usage(argv[0]);

		ProgramOpts opts;
		opts.connCount = std::stoi(argv[1]);

		for (auto a = argv + 2; a != argv + argc; ++a)
			opts.addrs.push_back(NetAddr::from(*a));
		return opts;
	}
}


int main(int argc, char* argv[])
{
	auto opts = parseArgs(argc, argv);

	std::thread(&statThread).detach();

	initEventLib();

	auto base = makeEventBase();

	EvhttpConnections evcons = makeConnections(base.get(), opts.addrs, opts.connCount);
	for (auto& evcon : evcons)
		sendReq(evcon.get());

	event_base_dispatch(base.get());
	return EXIT_SUCCESS;
}

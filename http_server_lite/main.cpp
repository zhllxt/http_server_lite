#ifndef ASIO2_ENABLE_SSL
#define ASIO2_ENABLE_SSL
#endif

#include <asio2/http/http_server.hpp>
#include <asio2/http/https_server.hpp>
#include <asio2/external/fmt.hpp>
#include <asio2/external/json.hpp>
#include <asio2/util/string.hpp>
#include <fstream>

int main()
{
	using json = nlohmann::json;

	std::ifstream file("config.json", std::ios::in | std::ios::binary);
	if (!file)
	{
		fmt::print("open the config file 'config.json' failed.\n");
		return 0;
	}

	json cfg;
	try
	{
		cfg = json::parse(file);
	}
	catch (json::exception const& e)
	{
		fmt::print("load the config file 'config.json' failed: {}\n", e.what());
		return 0;
	}

	if (!cfg.is_array())
	{
		fmt::print("the content of the config file 'config.json' is incorrect.\n");
		return 0;
	}

	asio2::iopool iopool;

	// iopool must start first, othwise the server.start will blocked forever.
	iopool.start();

	std::vector<std::shared_ptr<asio2::http_server >> http_servers;
	std::vector<std::shared_ptr<asio2::https_server>> https_servers;

	auto start_server = [](auto server,
		std::string host, std::uint16_t port, std::string path, std::string index) mutable
	{
		server->set_root_directory(path);

		// can't capture the "server" into the lambda, it will case the shared_ptr circular reference
		server->bind_start([host, port, pserver = server.get()]()
		{
			if (asio2::get_last_error())
				fmt::print("start http server failure : {} {} {}\n",
					host, port, asio2::last_error_msg());
			else
				fmt::print("start http server success : {} {}\n",
					pserver->listen_address(), pserver->listen_port());
		}).bind_stop([host, port]()
		{
			fmt::print("stop http server success : {} {} {}\n",
				host, port, asio2::last_error_msg());
		});

		// If no method is specified, GET and POST are both enabled by default.
		server->bind("/", [index](http::web_request& req, http::web_response& rep)
		{
			asio2::ignore_unused(req, rep);

			rep.fill_file(index);
		});

		server->bind("*", [](http::web_request& req, http::web_response& rep)
		{
			rep.fill_file(req.target());
		});

		server->bind_not_found([](http::web_request& req, http::web_response& rep)
		{
			asio2::ignore_unused(req);

			rep.fill_page(http::status::not_found);
		});

		server->start(host, port);
	};

	for (auto& site : cfg)
	{
		try
		{
			std::string protocol = site["protocol"].get<std::string>();
			std::string     host = site["host"].get<std::string>();
			std::uint16_t   port = site["port"].get<std::uint16_t>();
			std::string     path = site["path"].get<std::string>();
			std::string    index = site["index"].get<std::string>();

			if (protocol.empty())
			{
				fmt::print("Must specify protocol.\n");
				continue;
			}

			if (host.empty())
			{
				host = "0.0.0.0";
				fmt::print("The host is empty, will use {} to instead.\n", host);
			}

			if (path.empty())
			{
				fmt::print("Must specify path.\n");
				continue;
			}

			if (index.empty())
			{
				index = "index.html";
				fmt::print("The index is empty, will use {} to instead.\n", index);
			}

			if /**/ (asio2::iequals(protocol, "http"))
			{
				std::shared_ptr<asio2::http_server> http_server =
					std::make_shared<asio2::http_server>(iopool.get());
				start_server(http_server, host, port, path, index);
				http_servers.emplace_back(http_server);
			}
			else if (asio2::iequals(protocol, "https"))
			{
				std::string cert_file = site["cert_file"].get<std::string>();
				std::string key_file = site["key_file"].get<std::string>();

				if (cert_file.empty())
				{
					fmt::print("Must specify cert_file.\n");
					continue;
				}
				if (key_file.empty())
				{
					fmt::print("Must specify key_file.\n");
					continue;
				}

				std::shared_ptr<asio2::https_server> https_server =
					std::make_shared<asio2::https_server>(asio::ssl::context::sslv23, iopool.get());
				https_server->set_cert_file("", cert_file, key_file, "");
				https_server->set_dh_file(cert_file);
				start_server(https_server, host, port, path, index);
				https_servers.emplace_back(https_server);
			}
			else
			{
				fmt::print("invalid protocol: {}\n", protocol);
			}
		}
		catch (json::exception const& e)
		{
			fmt::print("read the config file 'config.json' failed: {}\n", e.what());
			return 0;
		}
	}

	iopool.wait_signal(SIGINT, SIGTERM);

	// must call iopool.stop() before exit.
	iopool.stop();

	fmt::print("progress exited.\n");

	return 0;
}

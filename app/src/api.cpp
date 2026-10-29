#include "api.h"

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/ServerSocket.h>
#include <spdlog/spdlog.h>

#include <rhydb/common/rhydb_directory.h>

#include "active_database.h"
#include "memory_monitor.h"
#include "request_handler_factory.h"
#include "rhydb_directory_watcher.h"

namespace rhydb_app {

int Api::runApi(const rhydb::config::RuntimeConfig& runtime_config) {
   SPDLOG_INFO("Starting RhyDB API");

   const Poco::Net::SocketAddress address(
      runtime_config.api_options.address, runtime_config.api_options.port
   );

   Poco::Net::ServerSocket server_socket;
   try {
      server_socket.bind(address, true);
      server_socket.listen();
   } catch (const Poco::Net::NetException& e) {
      SPDLOG_ERROR(
         "Failed to bind to {}:{}: {}",
         runtime_config.api_options.address,
         runtime_config.api_options.port,
         e.displayText()
      );
      return EXIT_FAILURE;
   }

   auto* const poco_parameter = new Poco::Net::HTTPServerParams;

   SPDLOG_INFO("Using {} queued http connections", runtime_config.api_options.max_connections);
   poco_parameter->setMaxQueued(runtime_config.api_options.max_connections);

   auto worker_threads_to_use = runtime_config.api_options.parallel_threads;
   if (worker_threads_to_use == 0) {
      worker_threads_to_use = static_cast<int32_t>(std::thread::hardware_concurrency());
   }
   SPDLOG_INFO("Using {} threads for http connections", worker_threads_to_use);
   poco_parameter->setMaxThreads(worker_threads_to_use);

   // Poco serves a kept-alive connection on one worker thread until it times out, even while it is
   // idle, so with more clients than threads, requests would wait for idle connections
   poco_parameter->setKeepAlive(false);

   // For better profiling, we do not want requests to allocate new threads in the thread pool.
   // Instead, just allocate all of them directly on start-up (by setting minCapacity)
   Poco::ThreadPool thread_pool(
      /* minCapacity = */ worker_threads_to_use,
      /* maxCapacity = */ worker_threads_to_use
   );

   auto database = std::make_shared<ActiveDatabase>();

   auto rhydb_request_handler_factory =
      std::make_unique<RhyDBRequestHandlerFactory>(runtime_config, database);

   const RhyDBDirectoryWatcher directory_watcher(
      rhydb::RhyDBDirectory{runtime_config.data_directory}, database
   );

   const MemoryMonitor memory_monitor{runtime_config.api_options.soft_memory_limit};

   // HTTPServer will erase the memory of the request_handler, therefore we call `release`
   Poco::Net::HTTPServer server(
      rhydb_request_handler_factory.release(), thread_pool, server_socket, poco_parameter
   );

   SPDLOG_INFO(
      "Listening on {}:{}", runtime_config.api_options.address, runtime_config.api_options.port
   );

   server.start();
   waitForTerminationRequest();
   server.stop();

   return Application::EXIT_OK;
}

}  // namespace rhydb_app

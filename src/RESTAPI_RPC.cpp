//
// Created by stephane bourque on 2021-06-28.
//
#include <cctype>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <future>
#include <numeric>
#include <chrono>
#include "RESTAPI_RPC.h"

#include "StorageService.h"
#include "DeviceRegistry.h"
#include "CommandManager.h"

namespace uCentral::RESTAPI_RPC {
	void SetCommandAsPending(GWObjects::CommandDetails &Cmd,
											 Poco::Net::HTTPServerRequest &Request,
											 Poco::Net::HTTPServerResponse &Response, RESTAPIHandler *Handler) {
		if (Storage()->AddCommand(Cmd.SerialNumber, Cmd, Storage::COMMAND_PENDING)) {
			Poco::JSON::Object RetObj;
			Cmd.to_json(RetObj);
			Handler->ReturnObject(Request, RetObj, Response);
			return;
		} else {
			Handler->ReturnStatus(Request, Response,
								  Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
			return;
		}
	}

	void WaitForCommand(GWObjects::CommandDetails &Cmd,
										Poco::JSON::Object  & Params,
										Poco::Net::HTTPServerRequest &Request,
										Poco::Net::HTTPServerResponse &Response,
										std::chrono::milliseconds D,
										Poco::JSON::Object * ObjectToReturn,
										RESTAPIHandler * Handler) {

		// 	if the command should be executed in the future, or if the device is not connected, then we should just add the command to
		//	the DB and let it figure out when to deliver the command.
		if(Cmd.RunAt || !DeviceRegistry()->Connected(Cmd.SerialNumber)) {
			SetCommandAsPending(Cmd, Request, Response, Handler);
			return;
		} else if(Cmd.RunAt==0 && DeviceRegistry()->Connected(Cmd.SerialNumber)) {
			auto Promise = std::make_shared<std::promise<Poco::JSON::Object::Ptr>>();
			std::future<Poco::JSON::Object::Ptr> Future = Promise->get_future();

			Cmd.Executed = time(nullptr);

			if (CommandManager()->SendCommand(Cmd.SerialNumber, Cmd.Command, Params, Promise, Cmd.UUID)) {
				auto Status = Future.wait_for(D);
				if (Status == std::future_status::ready) {
					auto Answer = Future.get();

					if (Answer->has("result") && Answer->isObject("result")) {
						auto ResultFields =
							Answer->get("result").extract<Poco::JSON::Object::Ptr>();
						if (ResultFields->has("status") && ResultFields->isObject("status")) {
							auto StatusInnerObj =
								ResultFields->get("status").extract<Poco::JSON::Object::Ptr>();
							if (StatusInnerObj->has("error"))
								Cmd.ErrorCode = StatusInnerObj->get("error");
							if (StatusInnerObj->has("text"))
								Cmd.ErrorText = StatusInnerObj->get("text").toString();
							std::stringstream ResultText;
							Poco::JSON::Stringifier::stringify(Answer->get("result"), ResultText);
							Cmd.Results = ResultText.str();
							Cmd.Status = "completed";
							Cmd.Completed = time(nullptr);

							//	Add the completed command to the database...
							Storage()->AddCommand(Cmd.SerialNumber, Cmd,Storage::COMMAND_COMPLETED);

							if(ObjectToReturn) {
								Handler->ReturnObject(Request, *ObjectToReturn, Response);
							} else {
								Poco::JSON::Object O;
								Cmd.to_json(O);
								Handler->ReturnObject(Request, O, Response);
							}
							return;
						}
					} else {
						SetCommandAsPending(Cmd, Request, Response, Handler);
						return;
					}
				} else {
					SetCommandAsPending(Cmd, Request, Response, Handler);
					return;
				}
			} else {
				SetCommandAsPending(Cmd, Request, Response, Handler);
				return;
			}
		}
	}

	bool WaitForRPC(GWObjects::CommandDetails &Cmd,
									Poco::Net::HTTPServerRequest &Request,
									Poco::Net::HTTPServerResponse &Response, uint64_t Timeout,
									bool ReturnValue,
									RESTAPIHandler * Handler) {

		if (DeviceRegistry()->Connected(Cmd.SerialNumber)) {
			GWObjects::CommandDetails ResCmd;
			while (Timeout > 0) {
				Timeout -= 1000;
				Poco::Thread::sleep(1000);
				if (Storage()->GetCommand(Cmd.UUID, ResCmd)) {
					if (ResCmd.Completed) {
						if (ReturnValue) {
							Poco::JSON::Object RetObj;
							ResCmd.to_json(RetObj);
							Handler->ReturnObject(Request, RetObj, Response);
						}
						return true;
					}
				}
			}
		}
		if (ReturnValue) {
			Poco::JSON::Object RetObj;
			Cmd.to_json(RetObj);
			Handler->ReturnObject(Request, RetObj, Response);
		}
		return false;
	}


}
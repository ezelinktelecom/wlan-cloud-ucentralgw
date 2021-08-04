//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include <iostream>
#include <fstream>
#include <cstdio>

#include "Daemon.h"
#include "FileUploader.h"
#include "StorageService.h"

#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/DynamicAny.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/PartHandler.h"
#include "Poco/Net/MessageHeader.h"
#include "Poco/CountingStream.h"
#include "Poco/StreamCopier.h"
#include "Poco/Exception.h"

#include "Utils.h"

namespace uCentral {
    class FileUploader *FileUploader::instance_ = nullptr;

    static const std::string URI_BASE{"/v1/upload/"};

    int FileUploader::Start() {
        Logger_.notice("Starting.");

        for(const auto & Svr: ConfigServersList_) {
            std::string l{"Starting: " +
                          Svr.Address() + ":" + std::to_string(Svr.Port()) +
                          " key:" + Svr.KeyFile() +
                          " cert:" + Svr.CertFile()};

            Logger_.information(l);

            Path_ = Daemon()->ConfigPath("ucentral.fileuploader.path","/tmp");

            auto Sock{Svr.CreateSecureSocket(Logger_)};

			Svr.LogCert(Logger_);
			if(!Svr.RootCA().empty())
				Svr.LogCas(Logger_);

            auto Params = new Poco::Net::HTTPServerParams;
            Params->setMaxThreads(16);
            Params->setMaxQueued(100);

            if(FullName_.empty()) {
                FullName_ = "https://" + Svr.Name() + ":" + std::to_string(Svr.Port()) + URI_BASE;
                Logger_.information(Poco::format("Uploader URI base is '%s'", FullName_));
            }
            auto NewServer = std::make_unique<Poco::Net::HTTPServer>(new FileUpLoaderRequestHandlerFactory(Logger_), Pool_, Sock, Params);
            NewServer->start();
            Servers_.push_back(std::move(NewServer));
        }

        MaxSize_ = 1000 * Daemon()->ConfigGetInt("ucentral.fileuploader.maxsize", 10000);

        return 0;
    }

    const std::string & FileUploader::FullName() {
        return FullName_;
    }

    //  if you pass in an empty UUID, it will just clean the list and not add it.
    bool FileUploader::AddUUID( const std::string & UUID) {
		SubMutexGuard		Guard(Mutex_);

        uint64_t Now = time(nullptr) ;

        // remove old stuff...
        for(auto i=OutStandingUploads_.cbegin();i!=OutStandingUploads_.end();) {
            if ((Now-i->second) > (60 * 30))
                OutStandingUploads_.erase(i++);
            else
                ++i;
        }

        if(!UUID.empty())
            OutStandingUploads_[UUID] = Now;

        return true;
    }

    bool FileUploader::ValidRequest(const std::string &UUID) {
		SubMutexGuard		Guard(Mutex_);

        return OutStandingUploads_.find(UUID)!=OutStandingUploads_.end();
    }

    void FileUploader::RemoveRequest(const std::string &UUID) {
		SubMutexGuard		Guard(Mutex_);
        OutStandingUploads_.erase(UUID);
    }

    class FileUploaderPartHandler: public Poco::Net::PartHandler
    {
    public:
		FileUploaderPartHandler(std::string UUID, Poco::Logger & Logger):
            UUID_(std::move(UUID)),
            Logger_(Logger)
        {
        }

        void handlePart(const Poco::Net::MessageHeader& Header, std::istream& Stream) override
        {
			try {
				for(const auto &i:Header) {
					std::cout << i.first << " = " << i.second << std::endl;
				}
				FileType_ = Header.get("Content-Type", "(unspecified)");
				auto SLength_ = Header.get("Content-Length","0");
				Name_ = "(unnamed)";
				if (Header.has("Content-Disposition")) {
					std::string Disposition;
					Poco::Net::NameValueCollection Parameters;
					Poco::Net::MessageHeader::splitParameters(Header["Content-Disposition"],
															  Disposition, Parameters);
					Name_ = Parameters.get("name", "(unnamed)");
				}

				std::cout << "Name: " << Name_ << std::endl;

				Poco::TemporaryFile TmpFile;
				std::string FinalFileName = FileUploader()->Path() + "/" + UUID_;
				Logger_.information(Poco::format("FILE-UPLOADER: uploading trace for %s", UUID_));

				Poco::CountingInputStream InputStream(Stream);
				Length_ = InputStream.chars();
				std::cout << "Length: " << Length_ << std::endl;
				std::ofstream OutputStream(TmpFile.path(), std::ofstream::out);
				Poco::StreamCopier::copyStream(InputStream, OutputStream);

				Length_ = InputStream.chars();

				std::cout << "Length 2: " << std::atoi(SLength_.c_str()) << std::endl;

				if (Length_ < FileUploader()->MaxSize()) {
					std::cout << "From: " << TmpFile.path().c_str() << "  To:" << FinalFileName.c_str() << std::endl;
					rename(TmpFile.path().c_str(), FinalFileName.c_str());
					Good_=true;
				}
				return;
			} catch (const Poco::Exception &E ) {
				std::cout << "Exception:" << E.what() << std::endl;
				Logger_.log(E);
			}
		}

        [[nodiscard]] uint64_t Length() const { return Length_; }
        [[nodiscard]] const std::string& Name() const { return Name_; }
        [[nodiscard]] const std::string& ContentType() const { return FileType_; }
		[[nodiscard]] bool Good() const { return Good_; }

    private:
        uint64_t        Length_=0;
		bool 			Good_=false;
        std::string     FileType_;
        std::string     Name_;
        std::string     UUID_;
        Poco::Logger    & Logger_;
    };


    class FormRequestHandler: public Poco::Net::HTTPRequestHandler
    {
    public:
        explicit FormRequestHandler(std::string UUID, Poco::Logger & L):
            UUID_(std::move(UUID)),
            Logger_(L)
        {
        }

        void handleRequest(Poco::Net::HTTPServerRequest& Request, Poco::Net::HTTPServerResponse& Response) override
        {
            try {
				FileUploaderPartHandler partHandler(UUID_,Logger_);

                Poco::Net::HTMLForm form(Request, Request.stream(), partHandler);

				Response.setChunkedTransferEncoding(true);
                Response.setContentType("application/json");

				Poco::JSON::Object	Answer;
                if (partHandler.Good()) {
					Answer.set("filename", UUID_);
					Answer.set("error", 0);
					Storage()->AttachFileToCommand(UUID_);
				} else {
					Answer.set("filename", UUID_);
					Answer.set("error", 13);
					Answer.set("errorText", "File could not be uploaded");
				}
				std::ostream &ResponseStream = Response.send();
				Poco::JSON::Stringifier::stringify(Answer, ResponseStream);
				return;
            }
            catch( const Poco::Exception & E )
            {
                Logger_.warning(Poco::format("Error occurred while performing upload. Error='%s'",E.displayText()));
            }
            catch( ... )
            {
            }
        }
    private:
        std::string     UUID_;
        Poco::Logger    & Logger_;
    };

    Poco::Net::HTTPRequestHandler *FileUpLoaderRequestHandlerFactory::createRequestHandler(const Poco::Net::HTTPServerRequest & Request) {

		Logger_.debug(Poco::format("REQUEST(%s): %s %s", uCentral::Utils::FormatIPv6(Request.clientAddress().toString()), Request.getMethod(), Request.getURI()));

        //  The UUID should be after the /v1/upload/ part...
        auto UUIDLocation = Request.getURI().find_first_of(URI_BASE);

        if( UUIDLocation != std::string::npos )
        {
            auto UUID = Request.getURI().substr(UUIDLocation+URI_BASE.size());
            if(FileUploader()->ValidRequest(UUID))
            {
                //  make sure we do not allow anyone else to overwrite our file
				FileUploader()->RemoveRequest(UUID);
                return new FormRequestHandler(UUID,Logger_);
            }
            else
            {
                Logger_.warning(Poco::format("Unknown UUID=%s",UUID));
            }
        }
        return nullptr;
    }

    void FileUploader::Stop() {
        Logger_.notice("Stopping ");
        for( const auto & svr : Servers_ )
            svr->stop();
    }

}  //  Namespace
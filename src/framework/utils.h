//
// Created by stephane bourque on 2022-10-25.
//

#pragma once

#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <shared_mutex>
#include <string>
#include <thread>

#include <dirent.h>

#include "Poco/Base64Decoder.h"
#include "Poco/Base64Encoder.h"
#include "Poco/File.h"
#include "Poco/Message.h"
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/Net/HTTPSClientSession.h"
#include "Poco/Net/NetworkInterface.h"
#include "Poco/SHA2Engine.h"
#include "Poco/StreamCopier.h"
#include "Poco/String.h"
#include "Poco/StringTokenizer.h"
#include "Poco/Thread.h"
#include "Poco/URI.h"
#include "Poco/zlib.h"

#include "framework/OpenWifiTypes.h"
#include "framework/ow_constants.h"

namespace OpenWifi::Utils {

	inline uint64_t Now() { return std::time(nullptr); };

	bool NormalizeMac(std::string &Mac);

	inline void SetThreadName(const char *name) {
#ifdef __linux__
		Poco::Thread::current()->setName(name);
		pthread_setname_np(pthread_self(), name);
#endif
#ifdef __APPLE__
		Poco::Thread::current()->setName(name);
		pthread_setname_np(name);
#endif
	}

	inline void SetThreadName(Poco::Thread &thr, const char *name) {
#ifdef __linux__
		thr.setName(name);
		pthread_setname_np(thr.tid(), name);
#endif
#ifdef __APPLE__
		thr.setName(name);
#endif
	}

	enum MediaTypeEncodings { PLAIN, BINARY, BASE64 };

	struct MediaTypeEncoding {
		MediaTypeEncodings Encoding = PLAIN;
		std::string ContentType;
	};

	[[nodiscard]] bool ValidSerialNumber(const std::string &Serial);
	[[nodiscard]] bool ValidSerialNumbers(const std::vector<std::string> &Serial);
	[[nodiscard]] bool ValidUUID(const std::string &UUID);
	[[nodiscard]] bool ValidHostname(const std::string &hostname);
	[[nodiscard]] bool ValidNumber(const std::string &number, bool isSigned);

	template <typename... Args> std::string ComputeHash(Args &&...args) {
		Poco::SHA2Engine E;
		auto as_string = [](auto p) {
			if constexpr (std::is_arithmetic_v<decltype(p)>) {
				return std::to_string(p);
			} else {
				return p;
			}
		};
		(E.update(as_string(args)), ...);
		return Poco::SHA2Engine::digestToHex(E.digest());
	}

	[[nodiscard]] std::vector<std::string> Split(const std::string &List, char Delimiter = ',');
	[[nodiscard]] std::string FormatIPv6(const std::string &I);
	void padTo(std::string &str, size_t num, char paddingChar = '\0');
	[[nodiscard]] std::string SerialToMAC(const std::string &Serial);
	uint64_t MACToInt(const std::string &MAC);
	[[nodiscard]] std::string ToHex(const std::vector<unsigned char> &B);

	using byte = std::uint8_t;

	[[nodiscard]] std::string base64encode(const byte *input, uint32_t size);
	[[nodiscard]] std::vector<byte> base64decode(const std::string &input);
	;
	bool ParseTime(const std::string &Time, int &Hours, int &Minutes, int &Seconds);
	bool ParseDate(const std::string &Time, int &Year, int &Month, int &Day);
	bool CompareTime(int H1, int H2, int M1, int M2, int S1, int S2);
	[[nodiscard]] std::string LogLevelToString(int Level);
	[[nodiscard]] uint64_t SerialNumberToInt(const std::string &S);
	[[nodiscard]] std::string IntToSerialNumber(uint64_t S);
	[[nodiscard]] bool SerialNumberMatch(const std::string &S1, const std::string &S2,
										 int Bits = 2);
	[[nodiscard]] uint64_t SerialNumberToOUI(const std::string &S);
	[[nodiscard]] uint64_t GetDefaultMacAsInt64();
	[[nodiscard]] uint64_t InitializeSystemId();
	[[nodiscard]] uint64_t GetSystemId();
	[[nodiscard]] bool ValidEMailAddress(const std::string &email);
	[[nodiscard]] std::string LoadFile(const Poco::File &F);
	void ReplaceVariables(std::string &Content, const Types::StringPairVec &P);
	[[nodiscard]] MediaTypeEncoding FindMediaType(const Poco::File &F);
	[[nodiscard]] std::string BinaryFileToHexString(const Poco::File &F);
	[[nodiscard]] std::string SecondsToNiceText(uint64_t Seconds);
	[[nodiscard]] bool wgets(const std::string &URL, std::string &Response);
	[[nodiscard]] bool wgetfile(const Poco::URI &uri, const std::string &FileName);
	[[nodiscard]] bool IsAlphaNumeric(const std::string &s);
	[[nodiscard]] std::string SanitizeToken(const std::string &Token);
	[[nodiscard]] bool ValidateURI(const std::string &uri);

	[[nodiscard]] std::uint64_t ConvertDate(const std::string &d);

	[[nodiscard]] inline uint8_t CalculateMacAddressHash(std::uint64_t value) {
		uint8_t hash = 0, i=6;
		while(i) {
			hash ^= (value & 0xFF) + 1;
			value >>= 8;
			--i;
		}
		return hash;
	}

	[[nodiscard]] inline uint8_t CalculateMacAddressHash(const std::string & value) {
		return CalculateMacAddressHash(MACToInt(value));
	}

	template <typename T> std::string int_to_hex(T i) {
		std::stringstream stream;
		stream << std::setfill('0') << std::setw(12) << std::hex << i;
		return stream.str();
	}

	inline bool SpinLock_Read(std::shared_mutex &M, volatile bool &Flag, uint64_t wait_ms = 100) {
		while (!M.try_lock_shared() && Flag) {
			Poco::Thread::yield();
			Poco::Thread::trySleep((long)wait_ms);
		}
		return Flag;
	}

	inline bool SpinLock_Write(std::shared_mutex &M, volatile bool &Flag, uint64_t wait_ms = 100) {
		while (!M.try_lock() && Flag) {
			Poco::Thread::yield();
			Poco::Thread::trySleep(wait_ms);
		}
		return Flag;
	}

	bool ExtractBase64CompressedData(const std::string &CompressedData,
									 std::string &UnCompressedData, uint64_t compress_sz);

	inline bool match(const char* first, const char* second)
	{
		// If we reach at the end of both strings, we are done
		if (*first == '\0' && *second == '\0')
			return true;

		// Make sure to eliminate consecutive '*'
		if (*first == '*') {
			while (*(first + 1) == '*')
				first++;
		}

		// Make sure that the characters after '*' are present
		// in second string. This function assumes that the
		// first string will not contain two consecutive '*'
		if (*first == '*' && *(first + 1) != '\0'
			&& *second == '\0')
			return false;

		// If the first string contains '?', or current
		// characters of both strings match
		if (*first == '?' || *first == *second)
			return match(first + 1, second + 1);

		// If there is *, then there are two possibilities
		// a) We consider current character of second string
		// b) We ignore current character of second string.
		if (*first == '*')
			return match(first + 1, second)
				   || match(first, second + 1);
		return false;
	}

	static inline std::uint64_t GetValue(FILE *file) {
		unsigned long v=0;
		char factor[32];
		if(fscanf(file, " %lu %31s", &v, factor)==2) {
			switch (factor[0]) {
			case 'k':
				return v * 1000;
			case 'M':
				return v * 1000000;
			case 'G':
				return v * 1000000000;
			}
		}
		return v;
	}

	inline bool getMemory(
		std::uint64_t &currRealMem, std::uint64_t &peakRealMem,
		std::uint64_t &currVirtMem, std::uint64_t &peakVirtMem) {

		// stores each word in status file
		char buffer[1024] = "";

		currRealMem = peakRealMem = currVirtMem = peakVirtMem = 0;

		// linux file contains this-process info
		FILE * file = std::fopen("/proc/self/status", "r");
		if (file == nullptr) {
			return false;
		}

		// read the entire file, recording mems in kB
		while (fscanf(file, " %1023s", buffer) == 1) {

			if (strcmp(buffer, "VmRSS:") == 0) {
				currRealMem= GetValue(file);
			} else if (strcmp(buffer, "VmHWM:") == 0) {
				peakRealMem= GetValue(file);
			} else if (strcmp(buffer, "VmSize:") == 0) {
				currVirtMem= GetValue(file);
			} else if (strcmp(buffer, "VmPeak:") == 0) {
				peakVirtMem= GetValue(file);
			}
		}
		fclose(file);

		return true;
	}

	inline int get_open_fds() {
		DIR *dp = opendir("/proc/self/fd");
		struct dirent *de;
		int count = -3; // '.', '..', dp

		if (dp == nullptr)
			return -1;
		while ((de = readdir(dp)) != nullptr)
			count++;
		(void)closedir(dp);

		return count;
	}

    inline std::uint32_t IPtoInt(const std::string &A) {
        Poco::Net::IPAddress    IP;
        std::uint32_t Result=0;

        if(Poco::Net::IPAddress::tryParse(A,IP)) {
            for(const auto i:IP.toBytes()) {
                Result <<= 8;
                Result += i;
            }
        }
        return Result;
    }

    inline bool ValidIP(const std::string &IPstr) {
        Poco::Net::IPAddress    IP;
        return Poco::Net::IPAddress::tryParse(IPstr,IP);
    }

    struct CSRCreationParameters {
        std::string Country, Province, City,
                    Organization, CommonName;
        int         bits=2048;
    };

    struct CSRCreationResults {
        std::string     CSR, PublicKey, PrivateKey;
    };

    bool CreateX509CSR(const CSRCreationParameters & Parameters, CSRCreationResults & Results);
    std::string generateStrongPassword(int minLength, int maxLength, int numDigits, int minLowercase, int minSpecial, int minUppercase);
    bool VerifyECKey(const std::string &key);
    bool VerifyRSAKey(const std::string &key);
    bool VerifyPrivateKey(const std::string &key);
    bool ValidX509Certificate(const std::string &Cert);
    bool ValidX509Certificate(const std::vector<std::string> &Certs);

    struct NAPTRRecord {
        std::string     name;
        std::string     ttl;
        std::string     rclass;
        std::string     rtype;
        uint32_t        order=0;
        uint32_t        preference=0;
        std::string     flags;
        std::string     service;
        std::string     regexp;
        std::string     replacement;
    };

// Function to query NAPTR records for a domain and return them in a vector
    std::vector<NAPTRRecord> getNAPTRRecords(const std::string& domain);
    struct SrvRecord {
        std::string     name;
        std::string     ttl;
        std::string     rclass;
        std::string     rtype;
        uint32_t        pref = 0;
        uint32_t        weight = 0;
        uint32_t        port = 0;
        std::string     srvname;
    };

    std::vector<SrvRecord> getSRVRecords(const std::string& domain);

    struct HostNameServerResult{
        std::string     Hostname;
        uint32_t        Port;
    };


} // namespace OpenWifi::Utils

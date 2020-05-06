// Includes
#include <iostream>
#include <signal.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <time.h>
#include <cstring>
#include <string>
#include <list>

// ++ DEFINES ++
// General
#define VERSION 1.00f
#define BUILD 1
#define SMALL_BUFFER 100
#define BIG_BUFFER 200

// Config
#define CFG_FILE_PATH "/etc/recUpdater/recUpdater.conf"
#define CFG_AMOUNT_ATTRIBUTES 8
#define CFG_DEF_IP_SERVICE "http://me.gandi.net"
#define CFG_DEF_API "https://dns.api.gandi.net/api/v5/"
#define CFG_DEF_SUCCESS_MSG "DNS Record Created"
#define CFG_DEF_TTL 18000
#define CFG_DEF_UPDATE_PERIOD_S 120
#define CFG_DEF_CURL_TIMEOUT_S 60
#define CFG_DEF_ENABLE_IPV6 true
#define CFG_DEF_LOGGING false
#define CFG_ERR_NONE 0
#define CFG_ERR_FILE 1
#define CFG_ERR_ATTR_MISSING 2
#define CFG_ERR_SYNTAX 3

// Logging
#define LOG_FILE_PATH "/var/log/recUpdater.log"

// Records
#define REC_ERR_NONE 0
#define REC_ERR_ARGUMENTS 1
#define REC_ERR_SPECIFER 2
#define REC_ERR_APIDOM 3

// Colors
#define COLOR_DEFAULT "\033[0m"
#define COLOR_RED "\033[0;31m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_GREEN "\033[0;32m"
// -- DEFINES --


// Struct ConfigContents
struct SConfigContents
{
	std::string strAPI, strIPService, strSuccessMsg;
	uint16_t iTTL, iUpdatePeriodS, iCurlTimeout;
	bool bEnableIPv6, bLogging;


	// Constructor
	SConfigContents()
	{
		// Assign default values
		strAPI = CFG_DEF_API;
		strIPService = CFG_DEF_IP_SERVICE;
		strSuccessMsg = CFG_DEF_SUCCESS_MSG;
		iTTL = CFG_DEF_TTL;
		iUpdatePeriodS = CFG_DEF_UPDATE_PERIOD_S;
		iCurlTimeout = CFG_DEF_CURL_TIMEOUT_S;
		bEnableIPv6 = CFG_DEF_ENABLE_IPV6;
		bLogging = CFG_DEF_LOGGING;

	} // Constructor

}; // Struct ConfigContents

// Struct DomRecords
struct SDomRecords
{
	bool bUpToData;
	std::string strAPIKey, strDomain;
	std::list<std::string> lstSubDomains;

	// Constructor
	SDomRecords() { bUpToData = false; }

}; // Struct DomRecords


// Function prototypes
bool setup(SConfigContents* psConfigContents, std::list<SDomRecords>* plstDomRecords, int argc, char* argv[], std::ofstream* pcLogFile);
std::string timeStamp();
uint8_t domRecordsFromCmdLine(std::list<SDomRecords>* plstDomRecords, int argc, char* argv[]);
std::string exec(const char* cCmd);
bool readIPAddress(std::string *pstrIPv4, std::string* pstrIPv6, SConfigContents sConfigContens);
bool updateRecord(SDomRecords *psDomRecords, SConfigContents sConfigContents, std::string strIPv4, std::string strIPv6, std::string *pstrErrorMsg = NULL);
void sigInterrupt(int iSignal);
uint8_t loadConfig(SConfigContents* psConfigContents);
bool generateConfig();

// Global variables
std::mutex g_mtxSigInterrupt;
std::condition_variable g_condSigInterrupt;
volatile sig_atomic_t g_sigInterrupt = 0;


// Main function
int main(int argc, char* argv[])
{
	// Variables
	SConfigContents sConfigContents;
	std::ofstream cLogFile;
	std::list<SDomRecords> lstRecords;
	std::string strCurIPv4, strCurIPv6, strLastIPv4, strLastIPv6, strErrorMsg;
	char cBuffer[SMALL_BUFFER];


	// Print program infos
	printf("[ARM] RecUpdater v.%.2f (Build %i)\n", VERSION, BUILD);

	// Try to setupt verything
	if (!setup(&sConfigContents, &lstRecords, argc, argv, &cLogFile))
	{
		// Print to terminal
		std::cerr << COLOR_RED << "[ERROR]: Initalization has failed! Exiting program..." << COLOR_DEFAULT << std::endl;
		return -1;
	}

	// Print to terminal
	std::cout << "Initalization was successfull. Starting main loop..." << std::endl;


	// Main loop
	while (!g_sigInterrupt)
	{
		// Try to read IP address
		if ((readIPAddress(&strCurIPv4, &strCurIPv6, sConfigContents)) && (!g_sigInterrupt))
		{
			// Check wether the IP address has changed
			if ((strCurIPv4 != strLastIPv4) || (strCurIPv6 != strLastIPv6))
			{
				// Unset update flag of all records
				for (std::list<SDomRecords>::iterator iRec = lstRecords.begin(); iRec != lstRecords.end(); iRec++)
					iRec->bUpToData = false;

				// Remeber current IP addresses
				strLastIPv4 = strCurIPv4;
				strLastIPv6 = strCurIPv6;
			}

			// Run through all records
			for (std::list<SDomRecords>::iterator iRec = lstRecords.begin(); ((iRec != lstRecords.end()) && (g_sigInterrupt == 0)); iRec++)
			{
				// Skip if no update required
				if (iRec->bUpToData)
					continue;


				// Format string and run through all subdomains and print them to console
				sprintf(cBuffer, "%s[UPDATE]: Updating record/s for \'%s\' (", timeStamp().c_str(), iRec->strDomain.c_str());
				for (std::list<std::string>::iterator iSDom = iRec->lstSubDomains.begin(); ((iSDom != iRec->lstSubDomains.end()) && (g_sigInterrupt == 0)); iSDom++)
					strcat(cBuffer, (*iSDom + ", ").c_str());

				// Format string and print to console
				cBuffer[strlen(cBuffer) - 2] = '\0';
				strcat(cBuffer, ")... ");
				std::cout << cBuffer << std::flush;

				// Write to logfile if enabled
				if (sConfigContents.bLogging)
					cLogFile << cBuffer << std::flush;


				// Try to update current record
				if (updateRecord(&(*iRec), sConfigContents, strCurIPv4, strCurIPv6, &strErrorMsg))
				{
					// Print to console and to logfile if enabled
					std::cout << COLOR_GREEN << "Done" << COLOR_DEFAULT << std::endl;
					if (sConfigContents.bLogging)
						cLogFile << "Done" << std::endl << std::flush;
				}
				// Check if update was canceled
				else if (g_sigInterrupt)
				{
					// Print to console and to logfile if enabled
					std::cerr << COLOR_YELLOW << "\b\bCanceled" << COLOR_DEFAULT << std::endl;
					if (sConfigContents.bLogging)
						cLogFile << "Canceled" << std::endl << std::flush;
				}
				// Updating failed
				else
				{
					// Print to console and to logfile if enabled
					std::cerr << COLOR_RED << "Failed\n" << timeStamp() << "[ERROR]: Failed to update record/s for \'" << iRec->strDomain << "\': " << strErrorMsg << "." << COLOR_DEFAULT << std::endl;
					if (sConfigContents.bLogging)
						cLogFile << "Failed\n" << timeStamp() << "[ERROR]: Failed to update record/s for \'" << iRec->strDomain << "\': " << strErrorMsg << "." << std::endl << std::flush;
				}
			}
		}
		// Failed to read IP address
		else
		{
			// Print to termninal
			std::cerr << COLOR_RED << timeStamp() << "[ERROR]: Something went wrong. Cannot get your IP from \'" << sConfigContents.strIPService << "\'" << COLOR_DEFAULT << std::endl;

			// Write to logfile if enabled
			if (sConfigContents.bLogging)
				cLogFile << timeStamp() << "[ERROR]: Something went wrong. Cannot get your IP from \'" << sConfigContents.strIPService << "\'" << std::endl << std::flush;
		}

		// Start sleeper thread
		std::thread thSleep([](SConfigContents sConfigContents) {
			std::unique_lock<std::mutex> lockSigInterrupt(g_mtxSigInterrupt);
			g_condSigInterrupt.wait_for(lockSigInterrupt, std::chrono::seconds(sConfigContents.iUpdatePeriodS), [] { return g_sigInterrupt; });
		}, sConfigContents);

		// Join sleeper thread
		thSleep.join();
	}

	// Close logfile and print to termnial
	cLogFile.close();
	std::cout << COLOR_RED << "\rterminated" << COLOR_DEFAULT << std::endl;


	// Success
	return 0;

} // Main function


//
// setup
//
// Task: Setup everything
//
bool setup(SConfigContents* psConfigContents, std::list<SDomRecords> *plstDomRecords, int argc, char* argv[], std::ofstream* pcLogFile)
{
	// Variables
	uint8_t iLoadRes;


	// Register interrupt signal, print to terminal and try to get records from command line
	signal(SIGINT, sigInterrupt);
	std::cout << "Reading command line... ";
	iLoadRes = domRecordsFromCmdLine(plstDomRecords, argc, argv);

	// Check if reading failed
	if (iLoadRes != REC_ERR_NONE)
	{
		// Print to terminal
		std::cout << COLOR_RED << "Failed" << std::endl;

		// Print corresponding error message to terminal
		if (iLoadRes == REC_ERR_SPECIFER)
			std::cerr << "[ERROR]: Unknown specifier! Sytax: recUpdater -k <APIKey> -d <domain> <subdomain1> <...>" << COLOR_DEFAULT << std::endl;
		else if (iLoadRes == REC_ERR_ARGUMENTS)
			std::cerr << "[ERROR]: Not enough arguments! Sytax: recUpdater -k <APIKey> -d <domain> <subdomain1> <...>" << COLOR_DEFAULT << std::endl;
		else
			std::cerr << "[ERROR]: No API key and/or domain specified! Sytax: recUpdater -k <APIKey> -d <domain> <subdomain1> <...>" << COLOR_DEFAULT << std::endl;

		// Failure
		return false;
	}


	// Print to terminal  and try to load the config
	std::cout << COLOR_GREEN << "OK" << COLOR_DEFAULT << std::endl;
	std::cout << "Loading config... ";
	iLoadRes = loadConfig(psConfigContents);

	// Check if loading failed
	if (iLoadRes != CFG_ERR_NONE)
	{
		// Print to terminal
		std::cout << COLOR_RED << "Failed" << COLOR_DEFAULT << std::endl;

		// Check if config is missing
		if (iLoadRes == CFG_ERR_FILE)
		{
			// Print to terminal
			std::cout << "Missing config. Generating new config with default settings... ";

			// Try to generate new config
			if (!generateConfig())
			{
				// Print to terminal
				std::cout << COLOR_RED << "Failed" << std::endl;
				std::cerr << "[ERROR]: Cannot create config file: " << std::strerror(errno) << "." << COLOR_DEFAULT << std::endl;

				// Failure
				return false;
			}
		}
		// Check if attributes missing
		else if (iLoadRes == CFG_ERR_ATTR_MISSING)
		{
			// Print to terminal and return
			std::cerr << COLOR_RED << "[ERROR]: Missing attributes in config. Please correct it and try again." << COLOR_DEFAULT << std::endl;
			return false;
		}
		// Syntax error
		else
		{
			// Print to terminal and return
			std::cerr << COLOR_RED << "[ERROR]: Syntax error in config. Please correct it and try again." << COLOR_DEFAULT << std::endl;
			return false;
		}
	}


	// Print to terminal
	std::cout << COLOR_GREEN << "OK" << COLOR_DEFAULT << std::endl;

	// Check if logging is enabled
	if (psConfigContents->bLogging)
	{
		// Print to terminal and try to open logfile
		std::cout << "Opening logfile... ";
		pcLogFile->open(LOG_FILE_PATH, std::ios_base::app);

		// Check if failed
		if (pcLogFile->fail())
		{
			// Print to terminal
			std::cout << COLOR_RED << "Failed" << COLOR_DEFAULT << std::endl;
			std::cerr << COLOR_YELLOW << "[WARNING]: Cannot open logfile: " << std::strerror(errno) << ". Logging disabled." << COLOR_DEFAULT << std::endl;

			// Close file and disable logging
			pcLogFile->close();
			psConfigContents->bLogging = false;
		}
		// Success
		else
		{
			// Print to terminal
			std::cout << COLOR_GREEN << "OK" << COLOR_DEFAULT << std::endl;
		}
	}
	// Logging disabled
	else
	{
		// Print to terminal
		std::cout << "[Config]: Logging disabled." << std::endl;
	}

	// Success
	return true;

} // setup

//
// domRecordsFromCmdLine
//
// Task: Get records from command line
//
uint8_t domRecordsFromCmdLine(std::list<SDomRecords> *plstDomRecords, int argc, char* argv[])
{
	// Variables
	SDomRecords sCurRecord;


	// Check if not enough start parameters have been passed
	if (argc < 6)
		return REC_ERR_ARGUMENTS;

	// Run through all start parameters
	for (uint8_t i = 1; i < argc; i++)
	{
		// Check whether it is a signal sign
		if (argv[i][0] == '-')
		{
			// Clear subdomains and increase iterator
			sCurRecord.lstSubDomains.clear();
			i++;

			// Check whether there are not enough parameters
			if (argc < (i + 2))
				return REC_ERR_ARGUMENTS;

			switch (argv[i - 1][1])
			{
				// API key
				case 'K':
				case 'k':
					// Clear domain and assign API key
					sCurRecord.strDomain.clear();
					sCurRecord.strAPIKey = argv[i];
					break;

				// Domain
				case 'D':
				case 'd':
					// Assign domain
					sCurRecord.strDomain = argv[i];
					break;

				// Error
				default:
					return REC_ERR_SPECIFER;
			}
		}
		// No signal sign
		else
		{
			// Check if record is valid
			if ((sCurRecord.strAPIKey.length() == 0) || (sCurRecord.strDomain.length() == 0))
				return REC_ERR_APIDOM;

			// Add current record to list
			sCurRecord.lstSubDomains.push_back(argv[i]);

			// Add record to list
			if ((argc == (i + 1)) || ((argc > (i + 1)) && (argv[i + 1][0] == '-')))
				plstDomRecords->push_back(sCurRecord);
		}
	}

	// Success
	return REC_ERR_NONE;

} // domRecordsFromCmdLine

//
// timeStamp
//
// Task: Get timastemp
//
std::string timeStamp()
{
	// Variables
	time_t tTime;
	tm* ptCurTime;
	char cTimeStamp[SMALL_BUFFER];


	// Get current time
	tTime = time(NULL);
	ptCurTime = localtime(&tTime);

	// Format string and return result
	sprintf(cTimeStamp, "[%.2i.%.2i.%i | %.2i:%.2i] ", ptCurTime->tm_mday, (ptCurTime->tm_mon + 1), (ptCurTime->tm_year + 1900), ptCurTime->tm_hour, ptCurTime->tm_min);
	return cTimeStamp;

} // timeStamp

//
// exec
//
// Task: Execute command and return result as string
//
std::string exec(const char* cCmd)
{
	// Variables
	char cBuffer[SMALL_BUFFER] = "";
	std::string strResult;
	FILE *pPipe = popen(cCmd, "r");


	// Check for failure
	if (!pPipe)
		return NULL;

	// Get result
	while (fgets(cBuffer, sizeof(cBuffer), pPipe) != NULL)
		strResult.append(cBuffer);

	// Close pipe and return result
	pclose(pPipe);
	return strResult;

} // exec


//
// readIPAddress
//
// Task: Get IP address of system
//
bool readIPAddress(std::string *pstrIPv4, std::string *pstrIPv6, SConfigContents sConfigContens)
{
	// Variables
	char cCommand[SMALL_BUFFER];


	// Format and execute command
	sprintf(cCommand, "curl -m %i -s4 %s", sConfigContens.iCurlTimeout, sConfigContens.strIPService.c_str());
	*pstrIPv4 = exec(cCommand);

	// Remove newline specifier if required
	if (pstrIPv4->length() > 0)
		pstrIPv4->pop_back();

	// Check if IPv6 support is enabled
	if (sConfigContens.bEnableIPv6)
	{
		// Format and execute command
		sprintf(cCommand, "curl -m %i -s6 %s", sConfigContens.iCurlTimeout, sConfigContens.strIPService.c_str());
		*pstrIPv6 = exec(cCommand);

		// Remove newline specifier if required
		if (pstrIPv6->length() > 0)
			pstrIPv6->pop_back();
	}

	// Check if the IP address could not be loaded
	if ((pstrIPv4->length() == 0) && (pstrIPv6->length() == 0))
		return false;

	// Success
	return true;

} // readIPAddress

//
// updateRecord
//
// Task: Update an record
//
bool updateRecord(SDomRecords *psDomRecords, SConfigContents sConfigContents, std::string strIPv4, std::string strIPv6, std::string *pstrErrorMsg)
{
	// Variables
	char cCommand[BIG_BUFFER];
	std::string strResult;
	int iMsgStartPos, iMsgEndPos;


	// Run through subdomains
	for (std::list<std::string>::iterator iSDom = psDomRecords->lstSubDomains.begin(); iSDom != psDomRecords->lstSubDomains.end(); iSDom++)
	{
		// Format and execute command
		sprintf(cCommand, "curl -m %i -s -XPUT -d \'{\"rrset_ttl\": \"%i\", \"rrset_values\": [\"%s\"]}\' -H \"X-Api-Key: %s\" -H \"Content-Type: application/json\" %s/domains/%s/records/%s/A", sConfigContents.iCurlTimeout, sConfigContents.iTTL, strIPv4.c_str(), psDomRecords->strAPIKey.c_str(), sConfigContents.strAPI.c_str(), psDomRecords->strDomain.c_str(), iSDom->c_str());
		strResult = exec(cCommand);

		// Convert result to lower case
		for (uint16_t i = 0; i < strResult.length(); i++)
			strResult[i] = tolower(strResult[i]);

		// Check if execution was successfull
		if (strResult.find(sConfigContents.strSuccessMsg) == std::string::npos)
		{
			// Assign error message if required
			if ((pstrErrorMsg != NULL) && ((iMsgStartPos = strResult.find("\"message\":m ")) != std::string::npos) && (strResult.length() >= (iMsgStartPos + 12)) && ((iMsgEndPos = strResult.find("\"", (iMsgStartPos + 12))) != std::string::npos))
				*pstrErrorMsg = strResult.substr((iMsgStartPos + 12), (iMsgEndPos - (iMsgStartPos + 12)));
			else if (pstrErrorMsg != NULL)
				*pstrErrorMsg = "Unknown error";

			// Failure
			return false;
		}

		// Check if IPv6 is enabled and valid
		if ((sConfigContents.bEnableIPv6) && (strIPv6.length() != 0))
		{
			// Format and execute command
			sprintf(cCommand, "curl -m %i -s -XPUT -d \'{\"rrset_ttl\": \"%i\", \"rrset_values\": [\"%s\"]}\' -H \"X-Api-Key: %s\" -H \"Content-Type: application/json\" %s/domains/%s/records/%s/A", sConfigContents.iCurlTimeout, sConfigContents.iTTL, strIPv6.c_str(), psDomRecords->strAPIKey.c_str(), sConfigContents.strAPI.c_str(), psDomRecords->strDomain.c_str(), iSDom->c_str());
			strResult = exec(cCommand);

			// Convert result to lower case
			for (uint16_t i = 0; i < strResult.length(); i++)
				strResult[i] = tolower(strResult[i]);

			// Check if execution was successfull
			if (strResult.find(sConfigContents.strSuccessMsg) == std::string::npos)
			{
				// Assign error message if required
				if ((pstrErrorMsg != NULL) && ((iMsgStartPos = strResult.find("\"message\":")) != std::string::npos) && (strResult.length() >= (iMsgStartPos + 12)) && ((iMsgEndPos = strResult.find("\"", (iMsgStartPos + 12))) != std::string::npos))
					*pstrErrorMsg = strResult.substr((iMsgStartPos + 12), (iMsgEndPos - (iMsgStartPos + 12)));
				else if (pstrErrorMsg != NULL)
					*pstrErrorMsg = "Unknown error";

				// Failure
				return false;
			}
		}

		// Set update flag
		psDomRecords->bUpToData = true;
	}

	// Success
	return true;

} // updateRecord


//
// sigInterrupt
//
// Task: Process terminate-interrupt
//
void sigInterrupt(int iSignal)
{
	// Set flag and notify main thread
	g_sigInterrupt = 1;
	g_condSigInterrupt.notify_one();

} // sigInterrupt


//
// loadConfig
//
// Task: Load the configuration from file
//
uint8_t loadConfig(SConfigContents* psConfigContents)
{
	// Variables
	std::string strCurLine;
	int iSepPos;
	uint8_t iAssignedAttr = 0;
	int8_t iIPv6Check, iLogCheck;


	// Try to open config file
	std::ifstream cCfgFile(CFG_FILE_PATH);

	// Check if failed
	if (cCfgFile.fail())
		return CFG_ERR_FILE;

	// Run trough file
	while (std::getline(cCfgFile, strCurLine))
	{
		// Convert to lower case
		for (uint8_t i = 0; i < strCurLine.length(); i++)
			strCurLine[i] = tolower(strCurLine[i]);

		// Try to find separation specifier
		iSepPos = strCurLine.find('=');

		// Check if current line is an attribute
		if (iSepPos != std::string::npos)
		{
			// Skip spaces from the right side of the specifier
			for (iSepPos = (iSepPos + 1); ((iSepPos < strCurLine.length()) && ((strCurLine[iSepPos] == ' ') || (strCurLine[iSepPos] == '\"'))); iSepPos++);

			// Remove spaces and quotes from the end of the string
			for (uint8_t i = (strCurLine.length() - 1); ((i >= 0) && ((strCurLine[i] == ' ') || (strCurLine[i] == '"'))); i--)
				strCurLine.erase(i);

			// Assign attribute
			if (strCurLine.find("ipservice") != std::string::npos)
				psConfigContents->strIPService = strCurLine.substr(iSepPos).c_str();
			else if (strCurLine.find("api") != std::string::npos)
				psConfigContents->strAPI = strCurLine.substr(iSepPos).c_str();
			else if (strCurLine.find("successmsg") != std::string::npos)
				psConfigContents->strSuccessMsg = strCurLine.substr(iSepPos).c_str();
			else if (strCurLine.find("ttl") != std::string::npos)
				psConfigContents->iTTL = atoi(strCurLine.substr(iSepPos).c_str()) >= 0 ? atoi(strCurLine.substr(iSepPos).c_str()) : 65535;
			else if (strCurLine.find("updateperiods") != std::string::npos)
				psConfigContents->iUpdatePeriodS = atoi(strCurLine.substr(iSepPos).c_str()) >= 0 ? atoi(strCurLine.substr(iSepPos).c_str()) : 65535;
			else if (strCurLine.find("curltimeout") != std::string::npos)
				psConfigContents->iCurlTimeout = atoi(strCurLine.substr(iSepPos).c_str()) >= 0 ? atoi(strCurLine.substr(iSepPos).c_str()) : 65535;
			else if (strCurLine.find("enableipv6") != std::string::npos)
				psConfigContents->bEnableIPv6 = iIPv6Check = strCurLine.substr(iSepPos).compare("true") != std::string::npos ? true : (strCurLine.substr(iSepPos).compare("false") != std::string::npos ? false : -1);
			else if (strCurLine.find("logging") != std::string::npos)
				psConfigContents->bLogging = iLogCheck = strCurLine.substr(iSepPos).compare("true") != std::string::npos ? true : (strCurLine.substr(iSepPos).compare("false") != std::string::npos ? false : -1);
			// Unknown attribute
			else
			{
				// Close file and return
				cCfgFile.close();
				return CFG_ERR_SYNTAX;
			}

			// Increase counter
			iAssignedAttr++;
		}
	}

	// Close file
	cCfgFile.close();

	// Check if not all attributte assigned
	if (iAssignedAttr != CFG_AMOUNT_ATTRIBUTES)
		return CFG_ERR_ATTR_MISSING;

	// Check for error in Syntax
	if ((psConfigContents->strIPService.length() == 0) || (psConfigContents->strSuccessMsg.length() == 0) || (psConfigContents->iTTL == 65535) || (psConfigContents->iUpdatePeriodS == 65535) || (iIPv6Check == -1) || (iLogCheck == -1))
		return CFG_ERR_SYNTAX;

	// Success
	return CFG_ERR_NONE;

} // loadConfig

//
// generateConfig
//
// Task: Create a new config file with default settings
//
bool generateConfig()
{
	// Variables
	std::filesystem::path ptDirPath = std::filesystem::path(CFG_FILE_PATH).parent_path();
	std::error_code errCreateDir;


	// Create parent directory if required
	if (!std::filesystem::exists(ptDirPath))
		if (!std::filesystem::create_directory(ptDirPath, ptDirPath.parent_path(), errCreateDir))
			return false;

	// Try to generate new config file
	std::ofstream cCfgFile(CFG_FILE_PATH);

	// Check if failed
	if (cCfgFile.fail())
		return false;

	// Write default config
	cCfgFile << "#####################################################" << std::endl;
	cCfgFile << "# Config file managing record updates for gandi.net #" << std::endl;
	cCfgFile << "#####################################################" << std::endl;
	cCfgFile << "\n# Service that is used to determine your own IP address" << std::endl;
	cCfgFile << "IPService=" << CFG_DEF_IP_SERVICE << std::endl;
	cCfgFile << "\n# The address of the API service" << std::endl;
	cCfgFile << "API=" << CFG_DEF_API << std::endl;
	cCfgFile << "\n# The message that must be received in order to view the record update as successful" << std::endl;
	cCfgFile << "SuccessMsg=\"" << CFG_DEF_SUCCESS_MSG << "\"" << std::endl;
	cCfgFile << "\n# The \"time to live\" of the record/s" << std::endl;
	cCfgFile << "TTL=" << CFG_DEF_TTL << std::endl;
	cCfgFile << "\n# Controls the update interval" << std::endl;
	cCfgFile << "UpdatePeriodS=" << CFG_DEF_UPDATE_PERIOD_S << std::endl;
	cCfgFile << "\n# Controls the timeout for CURL" << std::endl;
	cCfgFile << "CurlTimeoutS=" << CFG_DEF_CURL_TIMEOUT_S << std::endl;
	cCfgFile << "\n# Controls wether IPv6 support is enabled or disabled" << std::endl;
	cCfgFile << "EnableIPv6=" << (CFG_DEF_IP_SERVICE ? "true" : "false") << std::endl;
	cCfgFile << "\n# Controls wether logging is enabled or disabled" << std::endl;
	cCfgFile << "Logging=" << (CFG_DEF_LOGGING ? "true" : "false") << std::endl;

	// Close file
	cCfgFile.close();

	// Success
	return true;

} // generateConfig

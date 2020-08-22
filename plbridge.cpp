/**
 * @file plbridge.cpp
 *
 * https://github.com/helioz2000/pl20
 *
 * Author: Erwin Bejsta
 * July 2020
 *
 * Uses ModbusTag class for PLxx values
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <iostream>

#include <libconfig.h++>
#include <mosquitto.h>

#include "mqtt.h"
#include "pltag.h"
#include "hardware.h"
#include "plxx.h"
#include "plbridge.h"

using namespace std;
using namespace libconfig;

const char *build_date_str = __DATE__ " " __TIME__;
const int version_major = 1;
const int version_minor = 0;

#define CFG_FILENAME_EXT ".cfg"
#define CFG_DEFAULT_FILEPATH "/etc/"

#define MAIN_LOOP_INTERVAL_MINIMUM 50     // milli seconds
#define MAIN_LOOP_INTERVAL_MAXIMUM 2000   // milli seconds

#define MQTT_BROKER_DEFAULT "127.0.0.1"
#define MQTT_CLIENT_ID "plbridge"
#define MQTT_RECONNECT_INTERVAL 10

static string cpu_temp_topic = "";
static string cfgFileName;
static string execName;
static string mbSlaveStatusTopic;
bool exitSignal = false;
bool debugEnabled = false;
int plDebugLevel = 1;
bool mqttDebugEnabled = false;
bool runningAsDaemon = false;
time_t mqtt_connect_time = 0;   	// time the connection was initiated
time_t mqtt_next_connect_time = 0;	// time when next connect is scheduled
bool mqtt_connection_in_progress = false;
bool mqtt_retain_default = false;
std::string processName;
char *info_label_text;
useconds_t mainloopinterval = 250;   // milli seconds
updatecycle *updateCycles = NULL;	// array of update cycle definitions
PLtag *plReadTags = NULL;		// array of all PL read tags
PLtag *plWriteTags = NULL;		// array of all PL write tags
int plTagCount = -1;
uint32_t plTransactionDelay = 0;	// delay between modbus transactions
#define PL_DEVICE_MAX 254			// highest permitted PL device ID
#define PL_DEVICE_MIN 1				// lowest permitted PL device ID

Plxx *pl;

#pragma mark Proto types
void subscribe_tags(void);
void mqtt_connection_status(bool status);
void mqtt_topic_update(const struct mosquitto_message *message);
void mqtt_subscribe_tags(void);
void setMainLoopInterval(int newValue);
bool mqtt_publish_tag(PLtag *tag);
void mqtt_clear_tags(bool publish_noread, bool clear_retain);

//TagStore ts;
MQTT mqtt(MQTT_CLIENT_ID);
Config cfg;			// config file
Hardware hw(false);	// no screen

/**
 * log to console and syslog for daemon
 */
template<typename... Args> void log(int priority, const char * f, Args... args) {
	if (runningAsDaemon) {
		syslog(priority, f, args...);
	} else {
		fprintf(stderr, f, args...);
		fprintf(stderr, "\n");
	}
}

/**
 * Handle OS signals
 */
void sigHandler(int signum)
{
	char signame[10];
	switch (signum) {
		case SIGTERM:
			strcpy(signame, "SIGTERM");
			break;
		case SIGHUP:
			strcpy(signame, "SIGHUP");
			break;
		case SIGINT:
			strcpy(signame, "SIGINT");
			break;

		default:
			break;
	}

	log(LOG_INFO, "Received %s", signame);
	exitSignal = true;
}

void timespec_diff(struct timespec *start, struct timespec *stop, struct timespec *result) {
	if ((stop->tv_nsec - start->tv_nsec) < 0) {
		result->tv_sec = stop->tv_sec - start->tv_sec - 1;
		result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
	} else {
		result->tv_sec = stop->tv_sec - start->tv_sec;
	result->tv_nsec = stop->tv_nsec - start->tv_nsec;
	}
	return;
}

#pragma mark -- Config File functions

/**
 * Read configuration file.
 * @return true if success
 */
bool readConfig (void)
{
	//int ival;
	// Read the file. If there is an error, report it and exit.

	try
	{
		cfg.readFile(cfgFileName.c_str());
	}
	catch(const FileIOException &fioex)
	{
		std::cerr << "I/O error while reading file <" << cfgFileName << ">." << std::endl;
		return false;
	}
	catch(const ParseException &pex)
	{
		std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
				<< " - " << pex.getError() << std::endl;
		return false;
	}

	//log (LOG_INFO, "CFG file read OK");
	//std::cerr << cfgFileName << " read OK" <<endl;

	try {
		setMainLoopInterval(cfg.lookup("mainloopinterval"));
	} catch (const SettingNotFoundException &excp) {
	;
	} catch (const SettingTypeException &excp) {
		std::cerr << "Error in config file <" << excp.getPath() << "> is not an integer" << std::endl;
		return false;
	}

	// Read MQTT broker from config
	try {
		mqtt.setBroker(cfg.lookup("mqtt.broker"));
	} catch (const SettingNotFoundException &excp) {
		mqtt.setBroker(MQTT_BROKER_DEFAULT);
	} catch (const SettingTypeException &excp) {
		std::cerr << "Error in config file <" << excp.getPath() << "> is not a string" << std::endl;
		return false;
	}
	return true;
}

/**
 * Get integer value from config file
 */
bool cfg_get_int(const std::string &path, int &value) {
	if (!cfg.lookupValue(path, value)) {
		std::cerr << "Error in config file <" << path << ">" << std::endl;
		return false;
	}
	return true;
}

/**
 * Get string value from config file
 */
bool cfg_get_str(const std::string &path, std::string &value) {
	if (!cfg.lookupValue(path, value)) {
		std::cerr << "Error in config file <" << path << ">" << std::endl;
		return false;
	}
	return true;
}

#pragma mark -- Processing

/*
 * convert double byte values
 * unfortunately there is no standard conversion so this function
 * performs the various format conversions.
 *
 */
int pl_int_conversion(uint8_t lsb_addr, int lsb_val, int msb_val, int *value) {
	float fValue;
	switch(lsb_addr) {
		case 220:	// battery voltage
		case 237:	// sense voltage
			fValue = (float)(((msb_val * 256)+lsb_val)+38400)/5.120;    // value in millvolts
			*value = int(fValue);	// convert to int 
			break;
		case 188:	// Internal charge AH
		case 193:	// External charge AH
		case 198:	// Internal load AH
		case 203:	// External load AH
			*value = (msb_val * 8) + lsb_val;
			break;
		default:
			log(LOG_WARNING, "%s - unable to perfrom conversion for addr: %d", __func__, lsb_addr);
			return -1;
	}
	return 0;
}

/**
 * Read double byte value
 * @returns true for successful read
 * @param lsb_addr 
 * @param msb_addr
 * @param *value pointer to integer for read value
 */
int pl_read_int(uint8_t lsb_addr, uint8_t msb_addr, int *value) {
	int retVal = 0;
	uint8_t msb_val=0, lsb_val=0;
	int registerIntValue = 0;

	retVal = pl->read_RAM(lsb_addr, msb_addr, &lsb_val, &msb_val);
	if (retVal == 0) {
		retVal = pl_int_conversion(lsb_addr, lsb_val, msb_val, &registerIntValue);
		if (retVal == 0) {
			*value = registerIntValue;
		}
		//printf("%s - [%d] [%d] = %f %d\n", __FUNCTION__, msb_addr, lsb_addr, fValue, registerValue);
	}
	return retVal;
}

/**
 * Read single tag from PL device
 * @returns: true if successful read
 */
bool pl_read_tag(PLtag *tag) {
	uint8_t registerByteValue;
	uint8_t lsb_addr, msb_addr;
	int retVal;
	int address = tag->getAddress();
	int registerValue = 0;

	// single byte or multi byte address
	if (address <= 0xFF) { 	// single byte
		retVal = pl->read_RAM((uint8_t)address, &registerByteValue);
		registerValue = registerByteValue;
	} else {
		// two byte read - currently only suitable for Voltage reading
		//printf("%s - %s addr: %d\n", __func__, tag->getTopic(), address);
		lsb_addr = address & 0XFF;
		msb_addr = (address & 0xFF00) >> 8;
		retVal = pl_read_int(lsb_addr, msb_addr, &registerValue);
	}
	//printf("%s - %s: %d\n", __FUNCTION__, tag->getTopic(), registerValue);

	if (retVal == 0) {
		tag->setValue(registerValue);
	} else {
		tag->noreadNotify();
	}
	mqtt_publish_tag(tag);
	return retVal;
}

/**
 * process pl cyclic read update
 * @return false if there was nothing to process, otherwise true
 */
bool pl_read_process() {
	int index = 0;
	int tagIndex = 0;
	int *tagArray;
	bool retval = false;
	PLtag tag;
	time_t now = time(NULL);

	while (updateCycles[index].ident >= 0) {
		// ignore if cycle has no tags to process
		if (updateCycles[index].tagArray == NULL) {
			//printf("%s - no tags in index: %d\n", __FUNCTION__, index);
			index++; continue;
		}

		if (now >= updateCycles[index].nextUpdateTime) {
			// set next update cycle time
			updateCycles[index].nextUpdateTime = now + updateCycles[index].interval;
			// get array for tags
			tagArray = updateCycles[index].tagArray;
			if (tagArray != NULL) {
				// read each tag in the array
				tagIndex = 0;
				while (tagArray[tagIndex] >= 0) {
					tag = plReadTags[tagArray[tagIndex]];
					pl_read_tag(&plReadTags[tagArray[tagIndex]]);
					tagIndex++;
					usleep(plTransactionDelay);
				}
			}
			retval = true;
			//cout << now << " Update Cycle: " << updateCycles[index].ident << " - " << updateCycles[index].tagArraySize << " tags" << endl;
		}
		index++;
	}

	return retval;
}

/** Process all variables
 * @return true if at least one variable was processed
 * Note: the return value from this function is used
 * to measure processing time
 */
bool process() {
	bool retval = false;
	if (mqtt.isConnected()) {
		if (pl_read_process()) retval = true;
	}
//	var_process();	// don't want it in time measuring, doesn't take up much time
	return retval;
}

bool init_values(void)
{

	char info1[80], info2[80], info3[80], info4[80];

    // get hardware info
    hw.get_model_name(info1, sizeof(info1));
    hw.get_os_name(info2, sizeof(info2));
    hw.get_kernel_name(info3, sizeof(info3));
    hw.get_ip_address(info4, sizeof(info4));
    info_label_text = (char *)malloc(strlen(info1) +strlen(info2) +strlen(info3) +strlen(info4) +5);
    sprintf(info_label_text, "%s\n%s\n%s\n%s", info1, info2, info3, info4);
    if (!runningAsDaemon) {
	    printf(info_label_text);
        }
    //printf(info_label_text);
    return true;
}

#pragma mark MQTT

/** Initialise the tag database (tagstore)
 * @return false on failure
 */
bool init_tags(void) {
//	Tag* tp = NULL;
	std::string strValue;
//	int numTags, iVal, i;
//	bool bVal;

//	if (!init_hwtags()) return false;
	if (!cfg.exists("mqtt_tags")) {	// optional
		log(LOG_NOTICE,"configuration - parameter \"mqtt_tags\" does not exist");
		return true;
		}
/*
	Setting& mqttTagsSettings = cfg.lookup("mqtt_tags");
	numTags = mqttTagsSettings.getLength();

	mbWriteTags = new ModbusTag[numTags+1];
	//printf("%s - %d mqtt tags found\n", __func__, numTags);
	for (i=0; i < numTags; i++) {
		if (mqttTagsSettings[i].lookupValue("topic", strValue)) {
			tp = ts.addTag(strValue.c_str());
			tp->setSubscribe();
			tp->registerCallback(modbus_write_request, i);
			mbWriteTags[i].setTopic(strValue.c_str());
			if (mqttTagsSettings[i].lookupValue("slaveid", iVal))
				mbWriteTags[i].setSlaveId(iVal);
			if (mqttTagsSettings[i].lookupValue("address", iVal))
				mbWriteTags[i].setAddress(iVal);
			if (mqttTagsSettings[i].lookupValue("ignoreretained", bVal))
				mbWriteTags[i].setIgnoreRetained(bVal);
			if (mqttTagsSettings[i].exists("datatype")) {
				mqttTagsSettings[i].lookupValue("datatype", strValue);
				//printf("%s - %s\n", __func__, strValue.c_str());
				mbWriteTags[i].setDataType(strValue[0]);
			}
		}
	}
	// Mark end of list
	mbWriteTags[i].setSlaveId(MODBUS_SLAVE_MAX +1);
	mbWriteTags[i].setUpdateCycleId(-1);
	//printf("%s - allocated %d tags, last is index %d\n", __func__, i, i-1);
*/
	return true;
}

void mqtt_connect(void) {
	if (mqttDebugEnabled)
		printf("%s - attempting to connect to mqtt broker %s.\n", __func__, mqtt.broker());
	mqtt.connect();
	mqtt_connection_in_progress = true;
	mqtt_connect_time = time(NULL);
	mqtt_next_connect_time = 0;
	//printf("%s - Done\n", __func__);
}

/**
 * Initialise the MQTT broker and register callbacks
 */
bool mqtt_init(void) {
	bool bValue;
	if (!runningAsDaemon) {
		if (cfg.lookupValue("mqtt.debug", bValue)) {
			mqttDebugEnabled = bValue;
			mqtt.setConsoleLog(mqttDebugEnabled);
			if (mqttDebugEnabled) printf("%s - mqtt debug enabled\n", __func__);
		}
	}
	if (cfg.lookupValue("mqtt.retain_default", bValue))
		mqtt_retain_default = bValue;
	mqtt.registerConnectionCallback(mqtt_connection_status);
	mqtt.registerTopicUpdateCallback(mqtt_topic_update);
	mqtt_connect();
	return true;
}

/**
 * Subscribe tags to MQTT broker
 * Iterate over tag store and process every "subscribe" tag
 */
void mqtt_subscribe_tags(void) {
	//printf("%s - Start\n", __func__);
/*	Tag* tp = ts.getFirstTag();
	while (tp != NULL) {
		if (tp->isSubscribe()) {
			//printf("%s: %s\n", __func__, tp->getTopic());
			mqtt.subscribe(tp->getTopic());
		}
		tp = ts.getNextTag();
	}
	//printf("%s - Done\n", __func__);
*/
}

/**
 * callback function for MQTT
 * MQTT notifies a change in connection status by calling this function
 * This function is registered with MQTT during initialisation
 */
void mqtt_connection_status(bool status) {
	//printf("%s - %d\n", __func__, status);
	// subscribe tags when connection is online
	if (status) {
		log(LOG_INFO, "Connected to MQTT broker [%s]", mqtt.broker());
		mqtt_next_connect_time = 0;
		mqtt_connection_in_progress = false;
		mqtt.setRetain(mqtt_retain_default);
		mqtt_subscribe_tags();
	} else {
		if (mqtt_connection_in_progress) {
			mqtt.disconnect();
			// Note: the timeout is determined by OS network stack
			unsigned long timeout = time(NULL) - mqtt_connect_time;
			log(LOG_INFO, "mqtt connection timeout after %lds", timeout);
			mqtt_connection_in_progress = false;
		} else {
			log(LOG_WARNING, "Disconnected from MQTT broker [%s]", mqtt.broker());
		}
		if (!exitSignal) {
 			mqtt_next_connect_time = time(NULL) + MQTT_RECONNECT_INTERVAL;	// current time
 			log(LOG_INFO, "mqtt reconnect scheduled in %d seconds", MQTT_RECONNECT_INTERVAL);
 		}
	}
	//printf("%s - done\n", __func__);
}

/**
 * callback function for MQTT
 * MQTT notifies when a subscribed topic has received an update
 * @param topic: topic string
 * @param value: value as a string
 * Note: do not store the pointers "topic" & "value", they will be
 * destroyed after this function returns
 */
void mqtt_topic_update(const struct mosquitto_message *message) {
	//printf("%s - %s %s\n", __func__, topic, value);
/*	Tag *tp = ts.getTag(message->topic);
	if (tp == NULL) {
		fprintf(stderr, "%s: <%s> not  in ts\n", __func__, message->topic);
	} else {
		tp->setValueIsRetained(message->retain);
		if (message->payload != NULL) {
			tp->setValue((const char*)message->payload);	// This will trigger a callback to modbus_write_request
		}
	}*/
}

/**
 * Publish tag to MQTT
 * @param tag: ModbusTag to publish
 *
 */

bool mqtt_publish_tag(PLtag *tag) {
	if (!mqtt.isConnected()) return false;
	if (tag->getTopicString().empty()) return true;	// don't publish if topic is empty
	// Publish value if read was OK
	if (!tag->isNoread()) {
		mqtt.publish(tag->getTopic(), tag->getFormat(), tag->getScaledValue(), tag->getPublishRetain());
		//printf("%s - %s \n", __FUNCTION__, tag->getTopic());
		return true;
	}
	//printf("%s - NoRead: %s \n", __FUNCTION__, tag->getTopic());
	// Handle Noread
	if (!tag->noReadIgnoreExceeded()) return true;		// ignore noread, do nothing
	// noreadignore is exceeded, need to take action
	switch (tag->getNoreadAction()) {
	case 0:	// publish null value
		mqtt.clear_retained_message(tag->getTopic());
		break;
	case 1:	// publish noread value
		mqtt.publish(tag->getTopic(), tag->getFormat(), tag->getNoreadValue(), tag->getPublishRetain());
		break;
	default:
		// do nothing (default, -1)
		break;
	}

	return true;
}

/**
 * Publish noread value to all tags (normally done on program exit)
 * @param publish_noread: publish the "noread" value of the tag
 * @param clear_retain: clear retained value from broker's persistance store
 */
void mqtt_clear_tags(bool publish_noread = true, bool clear_retain = true) {

	int index = 0, tagIndex = 0;
	int *tagArray;
	PLtag plTag;
	//printf("%s", __func__);

	// Iterate over pl tag array
	//mqtt.setRetain(false);
	while (updateCycles[index].ident >= 0) {
		// ignore if cycle has no tags to process
		if (updateCycles[index].tagArray == NULL) {
			index++; continue;
		}
		// get array for tags
		tagArray = updateCycles[index].tagArray;
		// read each tag in the array
		tagIndex = 0;
		while (tagArray[tagIndex] >= 0) {
			plTag = plReadTags[tagArray[tagIndex]];
			if (publish_noread) {}
				mqtt.publish(plTag.getTopic(), plTag.getFormat(), plTag.getNoreadValue(), plTag.getPublishRetain());
				//mqtt_publish_tag(mbTag, true);			// publish noread value
			if (clear_retain) {}
				mqtt.clear_retained_message(plTag.getTopic());	// clear retained status
			tagIndex++;
		}
		index++;
	}	// while 

	// Iterate over local tags (e.g. CPU temp)
/*
	Tag *tag = ts.getFirstTag();
	while (tag != NULL) {
		if (tag->isPublish()) {
			mqtt.clear_retained_message(tag->getTopic());
		}
		tag = ts.getNextTag();
	}
*/
}

#pragma mark PLxx

/**
 * assign tags to update cycles
 * generate arrays of tags assigned ot the same updatecycle
 * 1) iterate over update cycles
 * 2) count tags which refer to update cycle
 * 3) allocate array for those tags
 * 4) fill array with index of tags that match update cycle
 * 5) assign array to update cycle
 * 6) go back to 1) until all update cycles have been matched
 */
bool pl_assign_updatecycles () {
	int updidx = 0;
	int plTagIdx = 0;
	int cycleIdent = 0;
	int matchCount = 0;
	int *intArray = NULL;
	int arIndex = 0;
	// iterate over updatecycle array
	while (updateCycles[updidx].ident >= 0) {
		cycleIdent = updateCycles[updidx].ident;
		updateCycles[updidx].tagArray = NULL;
		updateCycles[updidx].tagArraySize = 0;
		// iterate over mbReadTags array
		plTagIdx = 0;
		matchCount = 0;
		while (plReadTags[plTagIdx].updateCycleId() >= 0) {
			// count tags with cycle id match
			if (plReadTags[plTagIdx].updateCycleId() == cycleIdent) {
				matchCount++;
				//cout << cycleIdent <<" " << mbReadTags[mbTagIdx].getAddress() << endl;
			}
			plTagIdx++;
		}
		// skip to next cycle update if we have no matching tags
		if (matchCount < 1) {
			updidx++;
			continue;
		}
		// -- We have some matching tags
		// allocate array for tags in this cycleupdate
		intArray = new int[matchCount+1];			// +1 to allow for end marker
		// fill array with matching tag indexes
		plTagIdx = 0;
		arIndex = 0;
		while (plReadTags[plTagIdx].updateCycleId() >= 0) {
			// count tags with cycle id match
			if (plReadTags[plTagIdx].updateCycleId() == cycleIdent) {
				intArray[arIndex] = plTagIdx;
				arIndex++;
			}
			plTagIdx++;
		}
		// mark end of array
		intArray[arIndex] = -1;
		// add the array to the update cycles
		updateCycles[updidx].tagArray = intArray;
		updateCycles[updidx].tagArraySize = arIndex;
		// next update index
		updidx++;
	}
	return true;
}

/**
 * read tag configuration for one PL device from config file
 */
bool pl_config_tags(Setting& plTagsSettings, uint8_t deviceId) {
	int tagIndex;
	int tagAddress;
	int tagUpdateCycle;
	string strValue;
	float fValue;
	int intValue;
	bool bValue;

	int numTags = plTagsSettings.getLength();
	if (numTags < 1) {
		cout << "No tags Found " << endl;
		return true;		// permissible condition
	}

	for (tagIndex = 0; tagIndex < numTags; tagIndex++) {
		if (plTagsSettings[tagIndex].lookupValue("address", tagAddress)) {
			plReadTags[plTagCount].setAddress(tagAddress);
			plReadTags[plTagCount].setSlaveId(deviceId);
		} else {
			log(LOG_WARNING, "Error in config file, tag address missing");
			continue;		// skip to next tag
		}
		if (plTagsSettings[tagIndex].lookupValue("update_cycle", tagUpdateCycle)) {
			plReadTags[plTagCount].setUpdateCycleId(tagUpdateCycle);
		}
		if (plTagsSettings[tagIndex].lookupValue("group", intValue))
				plReadTags[plTagCount].setGroup(intValue);
		// is topic present? -> read mqtt related parametrs
		if (plTagsSettings[tagIndex].lookupValue("topic", strValue)) {
			plReadTags[plTagCount].setTopic(strValue.c_str());
			plReadTags[plTagCount].setPublishRetain(mqtt_retain_default);	// set to default
			if (plTagsSettings[tagIndex].lookupValue("retain", bValue))		// override default is required
				plReadTags[plTagCount].setPublishRetain(bValue);
			if (plTagsSettings[tagIndex].lookupValue("format", strValue))
				plReadTags[plTagCount].setFormat(strValue.c_str());
			if (plTagsSettings[tagIndex].lookupValue("multiplier", fValue))
				plReadTags[plTagCount].setMultiplier(fValue);
			if (plTagsSettings[tagIndex].lookupValue("offset", fValue))
				plReadTags[plTagCount].setOffset(fValue);
			if (plTagsSettings[tagIndex].lookupValue("noreadvalue", fValue))
				plReadTags[plTagCount].setNoreadValue(fValue);
			if (plTagsSettings[tagIndex].lookupValue("noreadaction", intValue))
				plReadTags[plTagCount].setNoreadAction(intValue);
			if (plTagsSettings[tagIndex].lookupValue("noreadignore", intValue))
				plReadTags[plTagCount].setNoreadIgnore(intValue);
		}
		//cout << "Tag " << plTagCount << " addr: " << tagAddress << " cycle: " << tagUpdateCycle;
		//cout << " Topic: " << plReadTags[plTagCount].getTopicString() << endl;
		plTagCount++;
	}
	return true;
}

/**
 * read slave configuration from config file
 */

bool pl_config_devices(Setting& plDeviceSettings) {
	int deviceId, numTags;
	string deviceName;
	bool deviceEnabled;

	// we need at least one slave in config file
	int numDevices = plDeviceSettings.getLength();
	if (numDevices < 1) {
		log(LOG_ERR, "Error in config file, no Modbus slaves found");
		return false;
	}

	// calculate the total number of tags for all configured slaves
	numTags = 0;
	for (int deviceIdx = 0; deviceIdx < numDevices; deviceIdx++) {
		if (plDeviceSettings[deviceIdx].exists("tags")) {
			if (!plDeviceSettings[deviceIdx].lookupValue("enabled", deviceEnabled)) {
				deviceEnabled = true;	// true is assumed if there is no entry in config file
			}
			if (deviceEnabled) {
				Setting& plTagsSettings = plDeviceSettings[deviceIdx].lookup("tags");
				numTags += plTagsSettings.getLength();
			}
		}
	}

	plReadTags = new PLtag[numTags+1];

	plTagCount = 0;
	// iterate through devices
	for (int deviceIdx = 0; deviceIdx < numDevices; deviceIdx++) {
		plDeviceSettings[deviceIdx].lookupValue("name", deviceName);
		if (plDeviceSettings[deviceIdx].lookupValue("id", deviceId)) {
			if (plDebugLevel > 0)
				printf("%s - processing Device %d (%s)\n", __func__, deviceId, deviceName.c_str());
		} else {
			log(LOG_ERR, "Config error - PL device ID missing in entry %d", deviceId+1);
			return false;
		}

		// get list of tags
		if (plDeviceSettings[deviceIdx].exists("tags")) {
			if (!plDeviceSettings[deviceIdx].lookupValue("enabled", deviceEnabled)) {
				deviceEnabled = true;	// true is assumed if there is no entry in config file
			}
			if (deviceEnabled) {
				Setting& plTagsSettings = plDeviceSettings[deviceIdx].lookup("tags");
				if (!pl_config_tags(plTagsSettings, deviceId)) {
					return false; }
			} else {
				log(LOG_NOTICE, "PL device %d (%s) disabled in config", deviceId, deviceName.c_str());
			}
		} else {
			log(LOG_NOTICE, "No tags defined for PL device %d", deviceId);
			// this is a permissible condition
		}
	}
	// mark end of array
	plReadTags[plTagCount].setUpdateCycleId(-1);
	plReadTags[plTagCount].setSlaveId(PL_DEVICE_MAX +1);
	return true;
}



/**
 * read update cycles from config file
 */
bool pl_config_updatecycles(Setting& updateCyclesSettings) {
	int idValue, interval, index;
	int numUpdateCycles = updateCyclesSettings.getLength();

	if (numUpdateCycles < 1) {
		log(LOG_ERR, "Error in config file, \"updatecycles\" missing");
		return false;
	}

	// allocate array
	updateCycles = new updatecycle[numUpdateCycles+1];

	for (index = 0; index < numUpdateCycles; index++) {
		if (updateCyclesSettings[index].lookupValue("id", idValue)) {
		} else {
			log(LOG_ERR, "Config error - cycleupdate ID missing in entry %d", index+1);
			return false;
		}
		if (updateCyclesSettings[index].lookupValue("interval", interval)) {
		} else {
			log(LOG_ERR, "Config error - cycleupdate interval missing in entry %d", index+1);
			return false;
		}
		updateCycles[index].ident = idValue;
		updateCycles[index].interval = interval;
		updateCycles[index].nextUpdateTime = time(0) + interval;
		//cout << "Update " << index << " ID " << idValue << " Interval: " << interval << " t:" << updateCycles[index].nextUpdateTime << endl;
	}
	// mark end of data
	updateCycles[index].ident = -1;
	updateCycles[index].interval = -1;

	return true;
}


/**
 * read PLxx configuration from config file
 */
bool pl_config() {
	// Configure update cycles
	try {
		Setting& updateCyclesSettings = cfg.lookup("updatecycles");
		if (!pl_config_updatecycles(updateCyclesSettings)) {
			return false; }
	} catch (const SettingNotFoundException &excp) {
		log(LOG_ERR, "Error in config file <%s> not found", excp.getPath());
		return false;
	} catch (const SettingTypeException &excp) {
		log(LOG_ERR, "Error in config file <%s> is wrong type", excp.getPath());
		return false;
	}


	// Configure pl devices
	try {
		Setting& plDeviceSettings = cfg.lookup("pldevices");
		if (!pl_config_devices(plDeviceSettings)) {
			return false; }
	} catch (const SettingNotFoundException &excp) {
		log(LOG_ERR, "Error in config file <%s> not found", excp.getPath());
		return false;
	} catch (const SettingTypeException &excp) {
		log(LOG_ERR, "Error in config file <%s> is not a string", excp.getPath());
		return false;
	} catch (const ParseException &excp) {
		log(LOG_ERR, "Error in config file - Parse Exception");
		return false;
	} catch (...) {
		log(LOG_ERR, "pl_config <pldevices> Error in config file (exception)");
		return false;
	}
	return true;
}

/**
 * get a valid baudrate for PLxx controller
 * @returns budrate constant from termios.h
 */
int pl_baudrate(int baud) {
	switch (baud) {
		case 300: return B300;
			break;
		case 1200: return B1200;
			break;
		case 2400: return B2400;
			break;
		default: return B9600;
	}
}


/**
 * initialize PL interface device
 * @returns false for configuration error, otherwise true
 */
bool init_pl()
{
	string pl_device;
	string strValue;
	int pl_baud = 9600;

	// check if mobus serial device is configured
	if (!cfg_get_str("plxx.device", pl_device)) {
		return true;
	}
	// get configuration serial device
	if (!cfg_get_int("plxx.baudrate", pl_baud)) {
			log(LOG_ERR, "plxx missing \"baudrate\" parameter for <%s>", pl_device.c_str());
		return false;
	}

	// Create PL object
	pl = new Plxx(pl_device.c_str(), pl_baudrate(pl_baud));

	if (pl == NULL) {
		log(LOG_ERR, "Can't initialize PLxx on %s\n", pl_device.c_str());
		return false;
	}

	log(LOG_INFO, "PL connection opened on port %s at %d baud", pl_device.c_str(), pl_baud);

	if (!pl_config()) return false;
	if (!pl_assign_updatecycles()) return false;

	return true;
}

#pragma mark Loops

/** 
 * set main loop interval to a valid setting
 * @param newValue the new main loop interval in ms
 */
void setMainLoopInterval(int newValue)
{
	int val = newValue;
	if (newValue < MAIN_LOOP_INTERVAL_MINIMUM) {
		val = MAIN_LOOP_INTERVAL_MINIMUM;
	}
	if (newValue > MAIN_LOOP_INTERVAL_MAXIMUM) {
		val = MAIN_LOOP_INTERVAL_MAXIMUM;
	}
	mainloopinterval = val;

	log(LOG_INFO, "Main Loop interval is %dms", mainloopinterval);
}

/** 
 * called on program exit
 */
void exit_loop(void) 
{
	bool bValue, clearonexit = false, noreadonexit = false;

	// how to handle mqtt broker published tags 
	// clear retain status for all tags?
	if (cfg.lookupValue("mqtt.clearonexit", bValue))
		clearonexit = bValue;
	// publish noread value for all tags?
	if (cfg.lookupValue("mqtt.noreadonexit", bValue)) 
		noreadonexit = bValue;
	if (noreadonexit || clearonexit)
		mqtt_clear_tags(noreadonexit, clearonexit);
	// free allocated memory
	// arrays of tags in cycleupdates
	int *ar, idx=0;
	while (updateCycles[idx].ident >= 0) {
		ar = updateCycles[idx].tagArray;
		if (ar != NULL) delete [] ar;		// delete array if one exists
		idx++;
	}

	delete [] updateCycles;
	delete pl;
}

/** 
 * Main program loop
 */
void main_loop()
{
	bool processing_success = false;
	//clock_t start, end;
	struct timespec starttime, endtime, difftime;
	useconds_t sleep_usec;
	//double delta_time;
	useconds_t processing_time;
	useconds_t min_time = 99999999, max_time = 0;
	useconds_t interval = mainloopinterval * 1000;	// convert ms to us

	// first call takes a long time (10ms)
	while (!exitSignal) {
	// run processing and record start/stop time
		clock_gettime(CLOCK_MONOTONIC, &starttime);
		processing_success = process();
		clock_gettime(CLOCK_MONOTONIC, &endtime);
		// calculate cpu time used [us]
		timespec_diff(&starttime, &endtime, &difftime);
		processing_time = (difftime.tv_nsec / 1000) + (difftime.tv_sec * 1000000);

		// store min/max times if any processing was done
		if (processing_success) {
			// calculate cpu time used [us]
			if (debugEnabled)
				printf("%s - process() took %dus\n", __func__, processing_time);
			if (processing_time > max_time) {
				max_time = processing_time;
			}
			if (processing_time < min_time) {
				min_time = processing_time;
			}
			//printf("%s - success (%dus)\n", __func__, processing_time);
		}
		// enter loop delay if needed
		// if cpu_time_used exceeds the mainLoopInterval
		// then bypass the loop delay
		if (interval > processing_time) {
			sleep_usec = interval - processing_time;  // sleep time in us
			//printf("%s - sleeping for %dus (%dus)\n", __func__, sleep_usec, processing_time);
			usleep(sleep_usec);
		}

		if (mqtt_next_connect_time > 0) {
 			if (time(NULL) >= mqtt_next_connect_time) {
 				mqtt_connect();
 			}
 		}

	}
	if (!runningAsDaemon)
		printf("CPU time for variable processing: %dus - %dus\n", min_time, max_time);
}

/** Display program usage instructions.
 * @param
 * @return
 */
static void showUsage(void) {
	cout << "usage:" << endl;
	cout << execName << "-cCfgFileName -d -h" << endl;
	cout << "c = name of config file (.cfg is added automatically)" << endl;
	cout << "d = enable debug mode" << endl;
	cout << "h = show help" << endl;
}

/** Parse command line arguments.
 * @param argc argument count
 * @param argv array of arguments
 * @return false to indicate program needs to abort
 */
bool parseArguments(int argc, char *argv[]) {
	char buffer[64];
	int i, buflen;
	int retval = true;
	execName = std::string(basename(argv[0]));
	cfgFileName = execName;

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			strcpy(buffer, argv[i]);
			buflen = strlen(buffer);
			if ((buffer[0] == '-') && (buflen >=2)) {
				switch (buffer[1]) {
				case 'c':
					cfgFileName = std::string(&buffer[2]);
					break;
				case 'd':
					debugEnabled = true;
					printf("Debug enabled\n");
					break;
				case 'h':
					showUsage();
					retval = false;
					break;
				default:
					log(LOG_NOTICE, "unknown parameter: %s", argv[i]);
					showUsage();
					retval = false;
					break;
				} // switch
				;
			} // if
		}  // for (i)
	}  // if (argc >1)
	// add config file extension
	cfgFileName += std::string(CFG_FILENAME_EXT);
	return retval;
}

int main (int argc, char *argv[])
{
	if ( getppid() == 1) runningAsDaemon = true;
	processName =  argv[0];

	if (! parseArguments(argc, argv) ) goto exit_fail;

	log(LOG_INFO,"[%s] PID: %d PPID: %d", argv[0], getpid(), getppid());
	log(LOG_INFO,"Version %d.%02d [%s] ", version_major, version_minor, build_date_str);

	// catch SIGTERM only if running as daemon (started via systemctl)
	// when run from command line SIGTERM provides a last resort method
	// of killing the process regardless of any programming errors.
	if (runningAsDaemon) {
		signal (SIGTERM, sigHandler);
	}

	// read config file
	if (! readConfig()) {
		log(LOG_ERR, "Error reading config file <%s>", cfgFileName.c_str());
		goto exit_fail;
	}

	if (!init_tags()) goto exit_fail;
	if (!mqtt_init()) goto exit_fail;
	if (!init_values()) goto exit_fail;
	if (!init_pl()) goto exit_fail;
	usleep(100000);
	main_loop();

	exit_loop();
	log(LOG_INFO, "exiting");
	exit(EXIT_SUCCESS);

exit_fail:
	log(LOG_INFO, "exit with error");
	exit(EXIT_FAILURE);
}

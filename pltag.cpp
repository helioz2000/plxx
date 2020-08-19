/**
 * @file pltag.cpp
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <sys/utsname.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pltag.h"

#include <stdexcept>
#include <iostream>

/*********************
 *      DEFINES
 *********************/
using namespace std;

/*********************
 * GLOBAL FUNCTIONS
 *********************/


/*********************
 * MEMBER FUNCTIONS
 *********************/

//
// Class Tag
//

PLtag::PLtag() {
	this->_value = 0.0;
	this->_address = 0;
	this->_group = 0;
	this->_topic = "";
	this->_slaveId = 0;
	this->_multiplier = 1.0;
	this->_offset = 0.0;
	this->_format = "%f";
	this->_noreadvalue = 0.0;
	this->_noreadaction = -1;	// do nothing
	this->_noreadignore = 0;
	this->_noreadcount = 0;
	this->_ignoreRetained = false;
	//printf("%s - constructor %d %s\\", __func__, this->_slaveId, this->_topic.c_str());
	//throw runtime_error("Class Tag - forbidden constructor");
}

PLtag::PLtag(const uint16_t addr) {
	this->_address = addr;
	this->_value = 0.0;
}

PLtag::~PLtag() {
	//cout << "Topic: <" << topic << ">" << endl;
	//printf("%s - destructor %d\n", __func__, address);
}

void PLtag::noreadNotify(void) {
	if (_noreadcount <= _noreadignore)	// if noreadignore is 0 noreadcount will still increment to 1
		_noreadcount++;					// a noreadcount > 0 indicates the tag is in noread state
}

bool PLtag::isNoread(void) {
	if (_noreadcount > 0) return true;
	else return false;
}

bool PLtag::noReadIgnoreExceeded(void) {
	if (_noreadcount > _noreadignore) return true;
	else return false;
}

void PLtag::setSlaveId(int newId) {
	_slaveId = newId;
}

uint8_t PLtag::getSlaveId(void) {
	return _slaveId;
}

void PLtag::setAddress(int newAddress) {
	_address = newAddress;
}

uint16_t PLtag::getAddress(void) {
	return _address;
}

void PLtag::setTopic(const char *topicStr) {
	if (topicStr != NULL) {
		_topic = topicStr;
	}
}

const char* PLtag::getTopic(void) {
	return _topic.c_str();
}

std::string PLtag::getTopicString(void) {
	return _topic;
}

void PLtag::setPublishRetain(bool newRetain) {
    _publish_retain = newRetain;
}

bool PLtag::getPublishRetain(void) {
    return _publish_retain;
}

void PLtag::setIgnoreRetained(bool newValue) {
	_ignoreRetained = newValue;
}

bool PLtag::getIgnoreRetained(void) {
	return _ignoreRetained;
}


void PLtag::setFormat(const char *formatStr) {
	if (formatStr != NULL) {
		_format = formatStr;
	}
}

const char* PLtag::getFormat(void) {
	return _format.c_str();
}

void PLtag::setValue(double newValue) {
	_value = newValue;
	_lastUpdateTime = time(NULL);
	_noreadcount = 0;
}

void PLtag::setValue(int newValue) {
	setValue( (double) newValue );
}

double PLtag::getValue(void) {
	return _value;
}

int PLtag::getIntValue(void) {
	return (int)_value;
}

bool PLtag::getBoolValue(void) {
	if (_value == 0.0)
		return false;
	else
		return true;
}

float PLtag::getScaledValue(void) {
	float fValue = (float) _value;
	fValue *= _multiplier;
	return fValue + _offset;
}

void PLtag::setMultiplier(float newMultiplier) {
	_multiplier = newMultiplier;
}

void PLtag::setOffset(float newOffset) {
	_offset = newOffset;
}

void PLtag::setUpdateCycleId(int ident) {
	_updatecycle_id = ident;
}

int PLtag::updateCycleId(void) {
	return _updatecycle_id;
}

void PLtag::setNoreadValue(float newValue) {
	_noreadvalue = newValue;
}

float PLtag::getNoreadValue(void) {
	return _noreadvalue;
}

void PLtag::setNoreadAction(int newValue) {
	_noreadaction = newValue;
}

int PLtag::getNoreadAction(void) {
	return _noreadaction;
}

void PLtag::setNoreadIgnore(int newValue) {
	_noreadignore = newValue;
}

int PLtag::getNoreadIgnore(void) {
	return _noreadignore;
}

/*
bool PLtag::setDataType(char newType) {
	switch (newType) {
	case 'i':
	case 'I':
		_dataType = 'i';
		break;
	case 'q':
	case 'Q':
		_dataType = 'q';
		break;
	case 'r':
	case 'R':
		_dataType = 'r';
		break;
	default:
		return false;
	}
	return true;
}

char PLtag::getDataType(void) {
	return _dataType;
}
*/

void PLtag::setGroup(int newValue) {
	_group = newValue;
}

int PLtag::getGroup(void) {
	return _group;
}


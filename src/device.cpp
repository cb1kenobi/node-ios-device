#include "device.h"
#include <sstream>

namespace node_ios_device {

/**
 * TODO
 */
Device::Device(napi_env env, std::string& udid, am_device& dev, CFRunLoopRef runloop) :
	usb(NULL),
	wifi(NULL),
	env(env),
	udid(udid) {

	portRelay = std::make_shared<PortRelay>(env, this, runloop);
	syslogRelay = std::make_shared<SyslogRelay>(env, this, runloop);

	std::shared_ptr<DeviceInterface> iface = changeInterface(dev, true);

	LOG_DEBUG_1("Device", "Getting device info for %s", udid.c_str());
	iface->connect();
	props["name"]            = iface->getProp(CFSTR("DeviceName"));
	props["buildVersion"]    = iface->getProp(CFSTR("BuildVersion"));
	props["cpuArchitecture"] = iface->getProp(CFSTR("CPUArchitecture"));
	props["deviceClass"]     = iface->getProp(CFSTR("DeviceClass"));
	props["deviceColor"]     = iface->getProp(CFSTR("DeviceColor"));
	props["hardwareModel"]   = iface->getProp(CFSTR("HardwareModel"));
	props["modelNumber"]     = iface->getProp(CFSTR("ModelNumber"));
	props["productType"]     = iface->getProp(CFSTR("ProductType"));
	props["productVersion"]  = iface->getProp(CFSTR("ProductVersion"));
	props["serialNumber"]    = iface->getProp(CFSTR("SerialNumber"));
	iface->disconnect();
}

/**
 * TODO
 */
std::shared_ptr<DeviceInterface> Device::changeInterface(am_device& dev, bool isAdd) {
	uint32_t type = ::AMDeviceGetInterfaceType(dev);
	if (type == 1) {
		if (isAdd && !usb) {
			LOG_DEBUG_1("Device::changeInterface", "Device %s connected via USB", udid.c_str())
			usb = std::make_shared<DeviceInterface>(udid, dev);
			return usb;
		} else if (!isAdd && usb) {
			LOG_DEBUG_1("Device::changeInterface", "Device %s disconnected via USB", udid.c_str())
			std::shared_ptr<DeviceInterface> tmp = usb;
			usb = NULL;
			return tmp;
		}
	} else if (type == 2) {
		if (isAdd && !wifi) {
			LOG_DEBUG_1("Device::changeInterface", "Device %s connected via Wi-Fi", udid.c_str())
			wifi = std::make_shared<DeviceInterface>(udid, dev);
			return wifi;
		} else if (!isAdd && wifi) {
			LOG_DEBUG_1("Device::removeInterface", "Device %s disconnected via Wi-Fi", udid.c_str())
			std::shared_ptr<DeviceInterface> tmp = wifi;
			wifi = NULL;
			return tmp;
		}
	} else {
		throw std::runtime_error("Unknown device interface type");
	}

	return NULL;
}

/**
 * Installs the specified app on the device.
 */
void Device::install(std::string& appPath) {
	if (usb) {
		usb->install(appPath);
	} else if (wifi) {
		wifi->install(appPath);
	} else {
		std::stringstream error;
		error << "No interfaces found for device " << udid;
		throw std::runtime_error(error.str());
	}
}

/**
 * TODO
 */
napi_value Device::toJS() {
	napi_value obj;

	NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CREATE_OBJECT", ::napi_create_object(env, &obj), NULL)

	#define SET_OBJECT_PROPERTY(key, value) \
		{ \
			napi_value tmp; \
			NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CREATE_STRING", ::napi_create_string_utf8(env, value.c_str(), NAPI_AUTO_LENGTH, &tmp), NULL) \
			NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_SET_NAMED_PROPERTY", ::napi_set_named_property(env, obj, key, tmp), NULL) \
		}

	#define COPY_OBJECT_PROPERTY(key) SET_OBJECT_PROPERTY(key, props[key])

	SET_OBJECT_PROPERTY("udid", udid)

	{
		napi_value ifaces, push, type;
		NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CREATE_ARRAY", ::napi_create_array(env, &ifaces), NULL)
		NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_GET_NAMED_PROPERTY", ::napi_get_named_property(env, ifaces, "push", &push), NULL)

		if (usb) {
			NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CREATE_STRING", ::napi_create_string_utf8(env, "USB", NAPI_AUTO_LENGTH, &type), NULL)
			NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CALL_FUNCTION", ::napi_call_function(env, ifaces, push, 1, &type, NULL), NULL)
		}

		if (wifi) {
			NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CREATE_STRING", ::napi_create_string_utf8(env, "Wi-Fi", NAPI_AUTO_LENGTH, &type), NULL)
			NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_CALL_FUNCTION", ::napi_call_function(env, ifaces, push, 1, &type, NULL), NULL)
		}

		NAPI_THROW_RETURN("Device::toJS", "ERR_NAPI_SET_NAMED_PROPERTY", ::napi_set_named_property(env, obj, "interfaces", ifaces), NULL)
	}

	COPY_OBJECT_PROPERTY("name")
	COPY_OBJECT_PROPERTY("buildVersion")
	COPY_OBJECT_PROPERTY("cpuArchitecture")
	COPY_OBJECT_PROPERTY("deviceClass")

	std::string deviceColor = props["deviceColor"];
	if (deviceColor == "0") {
		deviceColor = "White";
	} else if (deviceColor == "1") {
		deviceColor = "Black";
	} else if (deviceColor == "2") {
		deviceColor = "Silver";
	} else if (deviceColor == "3") {
		deviceColor = "Gold";
	} else if (deviceColor == "4") {
		deviceColor = "Rose Gold";
	} else if (deviceColor == "5") {
		deviceColor = "Jet Black";
	}
	SET_OBJECT_PROPERTY("deviceColor", deviceColor)

	COPY_OBJECT_PROPERTY("hardwareModel")
	COPY_OBJECT_PROPERTY("modelNumber")
	COPY_OBJECT_PROPERTY("productType")
	COPY_OBJECT_PROPERTY("productVersion")
	COPY_OBJECT_PROPERTY("serialNumber")

	return obj;
}

}

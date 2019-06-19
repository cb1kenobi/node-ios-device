#include "deviceman.h"

namespace node_ios_device {

/**
 * TODO
 */
DeviceMan::DeviceMan(napi_env env) : env(env), initialized(false), initTimer(NULL), runloop(NULL) {
	// wire up our dispatch change handler into Node's event loop, then unref it so that we don't
	// block Node from exiting
	uv_loop_t* loop;
	::napi_get_uv_event_loop(env, &loop);
	notifyChange.data = this;
	::uv_async_init(loop, &notifyChange, [](uv_async_t* handle) { ((DeviceMan*)handle->data)->dispatch(); });
	::uv_unref((uv_handle_t*)&notifyChange);
}

/**
 * TODO
 */
DeviceMan::~DeviceMan() {
	::uv_close((uv_handle_t*)&notifyChange, NULL);
	::AMDeviceNotificationUnsubscribe(deviceNotification);
	stopInitTimer();
	if (runloop) {
		::CFRunLoopStop(runloop);
		runloop = NULL;
	}
}

/**
 * TODO
 */
void DeviceMan::config(napi_value listener, WatchAction action) {
	if (action == Watch) {
		napi_ref ref;
		NAPI_THROW_RETURN("DeviceMan::dispatch", "ERROR_NAPI_CREATE_REFERENCE", ::napi_create_reference(env, listener, 1, &ref), )

		LOG_DEBUG("DeviceMan::config", "Adding listener")
		::uv_ref((uv_handle_t*)&notifyChange);
		{
			std::lock_guard<std::mutex> lock(listenersLock);
			listeners.push_back(ref);
		}

		// immediately fire the callback
		napi_value global, rval, devices = list();
		NAPI_THROW_RETURN("DeviceMan::dispatch", "ERR_NAPI_GET_GLOBAL", ::napi_get_global(env, &global), )
		NAPI_THROW_RETURN("DeviceMan::dispatch", "ERROR_NAPI_MAKE_CALLBACK", ::napi_make_callback(env, NULL, global, listener, 1, &devices, &rval), )
	} else {
		std::lock_guard<std::mutex> lock(listenersLock);
		for (auto it = listeners.begin(); it != listeners.end(); ) {
			napi_value fn;
			NAPI_THROW("DeviceMan::config", "ERR_NAPI_GET_REFERENCE_VALUE", ::napi_get_reference_value(env, *it, &fn))

			bool same;
			NAPI_THROW("DeviceMan::config", "ERR_NAPI_STRICT_EQUALS", ::napi_strict_equals(env, listener, fn, &same))

			if (same) {
				LOG_DEBUG("DeviceMan::config", "Removing listener")
				::uv_unref((uv_handle_t*)&notifyChange);
				it = listeners.erase(it);
			} else {
				++it;
			}
		}
	}
}

/**
 * TODO
 */
void DeviceMan::createInitTimer() {
	if (initialized) {
		return;
	}

	// set a timer for 250ms to unlock the initMutex
	CFRunLoopTimerContext timerContext = { 0, (void*)this, NULL, NULL, NULL };
	initTimer = ::CFRunLoopTimerCreate(
		kCFAllocatorDefault, // allocator
		CFAbsoluteTimeGetCurrent() + notificationWait, // fireDate
		0, // interval
		0, // flags
		0, // order
		[](CFRunLoopTimerRef timer, void* info) {
			LOG_DEBUG("DeviceMan::initTimer", "initTimer fired, unlocking init mutex")
			DeviceMan* runtime = (DeviceMan*)info;
			runtime->initialized = true;
			runtime->initMutex.unlock();
			runtime->stopInitTimer();
		},
		&timerContext
	);

	LOG_DEBUG("DeviceMan::createInitTimer", "Adding init timer to run loop")
	::CFRunLoopAddTimer(runloop, initTimer, kCFRunLoopCommonModes);
}

/**
 * Emits change events. This function is invoked by libuv on the main thread when a change
 * notification is sent from the background thread.
 */
void DeviceMan::dispatch() {
	napi_handle_scope scope;
	napi_value global, argv[2], listener, rval;

	NAPI_THROW_RETURN("DeviceMan::dispatch", "ERR_NAPI_OPEN_HANDLE_SCOPE", ::napi_open_handle_scope(env, &scope), )
	NAPI_THROW_RETURN("DeviceMan::dispatch", "ERR_NAPI_GET_GLOBAL", ::napi_get_global(env, &global), )
	NAPI_THROW_RETURN("DeviceMan::dispatch", "ERR_NAPI_CREATE_STRING", ::napi_create_string_utf8(env, "change", NAPI_AUTO_LENGTH, &argv[0]), )
	argv[1] = list();

	std::lock_guard<std::mutex> lock(listenersLock);
	size_t count = listeners.size();
	LOG_DEBUG_THREAD_ID_2("DeviceMan::dispatch", "Dispatching device changes to %ld %s", count, count == 1 ? "listener" : "listeners")

	for (auto const& ref : listeners) {
		NAPI_THROW_RETURN("DeviceMan::dispatch", "ERR_NAPI_GET_REFERENCE_VALUE", ::napi_get_reference_value(env, ref, &listener), )
		if (listener != NULL) {
			NAPI_THROW_RETURN("DeviceMan::dispatch", "ERROR_NAPI_MAKE_CALLBACK", ::napi_make_callback(env, NULL, global, listener, 2, argv, &rval), )
		}
	}
}

std::shared_ptr<Device> DeviceMan::getDevice(std::string& udid) {
	auto it = devices.find(udid);

	if (it == devices.end()) {
		std::string msg = "Device \"" + udid + "\" not found";
		throw std::runtime_error(msg);
	}

	return it->second;
}

/**
 * TODO
 */
void DeviceMan::init() {
	LOG_DEBUG_THREAD_ID("DeviceMan::init", "Starting background thread")
	std::thread(&DeviceMan::run, this).detach();

	LOG_DEBUG("DeviceMan::init", "Waiting for init mutex")

	// we need to wait until the run loop has had time to process the initial
	// device notifications, so we first lock the mutex ourselves, then we wait
	// 2 seconds for the run loop will unlock it
	initMutex.lock();

	// then we try to re-lock it, but we need to wait for the run loop thread
	// to unlock it first
	initMutex.try_lock_for(std::chrono::seconds(2));
}

/**
 * TODO
 */
void DeviceMan::install(std::string& udid, std::string& appPath) {
	auto it = devices.find(udid);

	if (it == devices.end()) {
		std::string msg = "Device \"" + udid + "\" not found";
		NAPI_THROW_ERROR("ERR_UDID_NOT_FOUND", msg.c_str(), msg.length(), )
	}

	try {
		it->second->install(appPath);
	} catch (std::exception& e) {
		const char* msg = e.what();
		LOG_DEBUG_1("DeviceMan::install", "%s", msg)
		NAPI_THROW_ERROR("ERR_UDID_NOT_FOUND", msg, ::strlen(msg), )
	}
}

/**
 * TODO
 */
napi_value DeviceMan::list() {
	napi_value rval;
	napi_value push;

	NAPI_THROW_RETURN("list", "ERR_NAPI_CREATE_ARRAY", ::napi_create_array(env, &rval), NULL)
	NAPI_THROW_RETURN("list", "ERR_NAPI_GET_NAMED_PROPERTY", ::napi_get_named_property(env, rval, "push", &push), NULL)

	std::lock_guard<std::mutex> lock(deviceMutex);
	for (auto const& it : devices) {
		napi_value device = it.second->toJS();
		NAPI_THROW_RETURN("list", "ERR_NAPI_CALL_FUNCTION", ::napi_call_function(env, rval, push, 1, &device, NULL), NULL)
	}

	return rval;
}

/**
 * The callback when a device notification is received.
 */
void DeviceMan::onDeviceNotification(am_device_notification_callback_info* info) {
	if (info->msg != ADNCI_MSG_CONNECTED && info->msg != ADNCI_MSG_DISCONNECTED) {
		return;
	}

	bool changed = false;

	LOG_DEBUG("DeviceMan::onDeviceNotification", "Resetting timer due to new device notification")
	stopInitTimer();

	std::string udid(::CFStringGetCStringPtr(::AMDeviceCopyDeviceIdentifier(info->dev), kCFStringEncodingUTF8));
	std::lock_guard<std::mutex> lock(deviceMutex);

	auto it = devices.find(udid);
	std::shared_ptr<Device> device = it != devices.end() ? it->second : NULL;

	if (device) {
		if (info->msg == ADNCI_MSG_CONNECTED) {
			changed = device->changeInterface(info->dev, true) != NULL;
		} else if (info->msg == ADNCI_MSG_DISCONNECTED) {
			changed = device->changeInterface(info->dev, false) != NULL;
			if (device->isDisconnected()) {
				devices.erase(udid);
			}
		}
	} else if (info->msg == ADNCI_MSG_CONNECTED) {
		try {
			device = std::make_shared<Device>(env, udid, info->dev, runloop);
			devices.insert(std::make_pair(udid, device));
			changed = true;
		} catch (std::exception& e) {
			LOG_DEBUG_1("DeviceMan::onDeviceNotification", "%s", e.what())
		}
	}

	createInitTimer();

	// we need to notify if devices changed and this must be done outside the
	// scopes above so that the mutex is unlocked
	if (changed) {
		::uv_async_send(&notifyChange);
	}
}

/**
 * The background thread that runs the actual runloop and notifies the main thread of events.
 */
void DeviceMan::run() {
	LOG_DEBUG_THREAD_ID("DeviceMan::run", "Initializing run loop")

	LOG_DEBUG("DeviceMan::run", "Subscribing to device notifications")
	::AMDeviceNotificationSubscribe([](am_device_notification_callback_info* info, void* arg) { ((DeviceMan*)arg)->onDeviceNotification(info); }, 0, 0, (void*)this, &deviceNotification);

	runloop = ::CFRunLoopGetCurrent();

	createInitTimer();

	LOG_DEBUG("DeviceMan::run", "Starting CoreFoundation run loop")
	::CFRunLoopRun();
}

/**
 * TODO
 */
void DeviceMan::stopInitTimer() {
	if (initTimer) {
		LOG_DEBUG("DeviceMan::stopInitTimer", "Removing init timer from run loop")
		::CFRunLoopTimerInvalidate(initTimer);
		::CFRunLoopRemoveTimer(runloop, initTimer, kCFRunLoopCommonModes);
		::CFRelease(initTimer);
		initTimer = NULL;
	}
}

} // end namespace node_ios_device

#include "node_usb.h"
#include <string.h>

#define STRUCT_TO_V8(TARGET, STR, NAME) \
	TARGET.DefineProperty(Napi::PropertyDescriptor::Value(#NAME, Napi::Number::New(env, (uint32_t) (STR).NAME), CONST_PROP));

#define CHECK_OPEN() \
	if (!self->device_handle){THROW_ERROR("Device is not opened");}

#define MAX_PORTS 7

Napi::FunctionReference Device::constructor;

Device::Device(const Napi::CallbackInfo & info) : Napi::ObjectWrap<Device>(info), device_handle(0), refs_(1) {
	device = info[0].As<Napi::External<libusb_device>>().Data();
	libusb_ref_device(device);
	byPtr.insert(std::make_pair(device, this));
	DEBUG_LOG("Created device %p", this);
	Constructor(info);
}

Device::~Device(){
	DEBUG_LOG("Freed device %p", this);
	byPtr.erase(device);
	libusb_close(device_handle);
	libusb_unref_device(device);
}

// Map pinning each libusb_device to a particular V8 instance
std::map<libusb_device*, Device*> Device::byPtr;

// Get a V8 instance for a libusb_device: either the existing one from the map,
// or create a new one and add it to the map.
Napi::Object Device::get(napi_env env, libusb_device* dev){
	auto it = byPtr.find(dev);
	if (it != byPtr.end()){
		return it->second->Value();
	} else {
		Napi::Object obj = Device::constructor.New({ Napi::External<libusb_device>::New(env, dev) });
		return obj;
	}
}

Napi::Value Device::Constructor(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	ENTER_CONSTRUCTOR_POINTER(Device, 1);
	auto obj = info.This().As<Napi::Object>();
	obj.DefineProperty(Napi::PropertyDescriptor::Value("busNumber", Napi::Number::New(env, libusb_get_bus_number(self->device)), CONST_PROP));
	obj.DefineProperty(Napi::PropertyDescriptor::Value("deviceAddress", Napi::Number::New(env, libusb_get_device_address(self->device)), CONST_PROP));

	Napi::Object v8dd = Napi::Object::New(env);
	obj.DefineProperty(Napi::PropertyDescriptor::Value("deviceDescriptor", v8dd, CONST_PROP));

	struct libusb_device_descriptor dd;
	CHECK_USB(libusb_get_device_descriptor(self->device, &dd));

	STRUCT_TO_V8(v8dd, dd, bLength)
	STRUCT_TO_V8(v8dd, dd, bDescriptorType)
	STRUCT_TO_V8(v8dd, dd, bcdUSB)
	STRUCT_TO_V8(v8dd, dd, bDeviceClass)
	STRUCT_TO_V8(v8dd, dd, bDeviceSubClass)
	STRUCT_TO_V8(v8dd, dd, bDeviceProtocol)
	STRUCT_TO_V8(v8dd, dd, bMaxPacketSize0)
	STRUCT_TO_V8(v8dd, dd, idVendor)
	STRUCT_TO_V8(v8dd, dd, idProduct)
	STRUCT_TO_V8(v8dd, dd, bcdDevice)
	STRUCT_TO_V8(v8dd, dd, iManufacturer)
	STRUCT_TO_V8(v8dd, dd, iProduct)
	STRUCT_TO_V8(v8dd, dd, iSerialNumber)
	STRUCT_TO_V8(v8dd, dd, bNumConfigurations)

	uint8_t port_numbers[MAX_PORTS];
	int ret = libusb_get_port_numbers(self->device, &port_numbers[0], MAX_PORTS);
	if (ret > 0) {
		Napi::Array array = Napi::Array::New(env, ret);
		for (int i = 0; i < ret; ++ i) {
			array.Set(i, Napi::Number::New(env, port_numbers[i]));
		}
		obj.DefineProperty(Napi::PropertyDescriptor::Value("portNumbers", array, CONST_PROP));
	}
	return info.This();
}

Napi::Object Device::cdesc2V8(napi_env env, libusb_config_descriptor * cdesc){
	Napi::Object v8cdesc = Napi::Object::New(env);

	STRUCT_TO_V8(v8cdesc, *cdesc, bLength)
	STRUCT_TO_V8(v8cdesc, *cdesc, bDescriptorType)
	STRUCT_TO_V8(v8cdesc, *cdesc, wTotalLength)
	STRUCT_TO_V8(v8cdesc, *cdesc, bNumInterfaces)
	STRUCT_TO_V8(v8cdesc, *cdesc, bConfigurationValue)
	STRUCT_TO_V8(v8cdesc, *cdesc, iConfiguration)
	STRUCT_TO_V8(v8cdesc, *cdesc, bmAttributes)
	// Libusb 1.0 typo'd bMaxPower as MaxPower
	v8cdesc.DefineProperty(Napi::PropertyDescriptor::Value("bMaxPower", Napi::Number::New(env, (uint32_t)cdesc->MaxPower), CONST_PROP));

	v8cdesc.DefineProperty(Napi::PropertyDescriptor::Value("extra", Napi::Buffer<const char>::Copy(env, (const char*)cdesc->extra, cdesc->extra_length), CONST_PROP));

	Napi::Array v8interfaces = Napi::Array::New(env, cdesc->bNumInterfaces);
	v8cdesc.DefineProperty(Napi::PropertyDescriptor::Value("interfaces", v8interfaces, CONST_PROP));

	for (int idxInterface = 0; idxInterface < cdesc->bNumInterfaces; idxInterface++) {
		int numAltSettings = cdesc->interface[idxInterface].num_altsetting;

		Napi::Array v8altsettings = Napi::Array::New(env, numAltSettings);
		v8interfaces.Set(idxInterface, v8altsettings);

		for (int idxAltSetting = 0; idxAltSetting < numAltSettings; idxAltSetting++) {
			const libusb_interface_descriptor& idesc =
				cdesc->interface[idxInterface].altsetting[idxAltSetting];

			Napi::Object v8idesc = Napi::Object::New(env);
			v8altsettings.Set(idxAltSetting, v8idesc);

			STRUCT_TO_V8(v8idesc, idesc, bLength)
			STRUCT_TO_V8(v8idesc, idesc, bDescriptorType)
			STRUCT_TO_V8(v8idesc, idesc, bInterfaceNumber)
			STRUCT_TO_V8(v8idesc, idesc, bAlternateSetting)
			STRUCT_TO_V8(v8idesc, idesc, bNumEndpoints)
			STRUCT_TO_V8(v8idesc, idesc, bInterfaceClass)
			STRUCT_TO_V8(v8idesc, idesc, bInterfaceSubClass)
			STRUCT_TO_V8(v8idesc, idesc, bInterfaceProtocol)
			STRUCT_TO_V8(v8idesc, idesc, iInterface)

			v8idesc.DefineProperty(Napi::PropertyDescriptor::Value("extra", Napi::Buffer<const char>::Copy(env, (const char*)idesc.extra, idesc.extra_length), CONST_PROP));

			Napi::Array v8endpoints = Napi::Array::New(env, idesc.bNumEndpoints);
			v8idesc.DefineProperty(Napi::PropertyDescriptor::Value("endpoints", v8endpoints, CONST_PROP));
			for (int idxEndpoint = 0; idxEndpoint < idesc.bNumEndpoints; idxEndpoint++){
				const libusb_endpoint_descriptor& edesc = idesc.endpoint[idxEndpoint];

				Napi::Object v8edesc = Napi::Object::New(env);
				v8endpoints.Set(idxEndpoint, v8edesc);

				STRUCT_TO_V8(v8edesc, edesc, bLength)
				STRUCT_TO_V8(v8edesc, edesc, bDescriptorType)
				STRUCT_TO_V8(v8edesc, edesc, bEndpointAddress)
				STRUCT_TO_V8(v8edesc, edesc, bmAttributes)
				STRUCT_TO_V8(v8edesc, edesc, wMaxPacketSize)
				STRUCT_TO_V8(v8edesc, edesc, bInterval)
				STRUCT_TO_V8(v8edesc, edesc, bRefresh)
				STRUCT_TO_V8(v8edesc, edesc, bSynchAddress)

				v8edesc.DefineProperty(Napi::PropertyDescriptor::Value("extra", Napi::Buffer<const char>::Copy(env, (const char*)edesc.extra, edesc.extra_length), CONST_PROP));
			}
		}
	}
	return v8cdesc;
}

Napi::Value Device::GetConfigDescriptor(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 0);
	libusb_config_descriptor* cdesc;
	CHECK_USB(libusb_get_active_config_descriptor(self->device, &cdesc));
	Napi::Object v8cdesc = Device::cdesc2V8(env, cdesc);
	libusb_free_config_descriptor(cdesc);
	return v8cdesc;
}

Napi::Value Device::GetAllConfigDescriptors(const Napi::CallbackInfo& info){
	ENTER_METHOD(Device, 0);
	libusb_config_descriptor * cdesc;
	struct libusb_device_descriptor dd;
	libusb_get_device_descriptor(self->device, &dd);
	Napi::Array v8cdescriptors = Napi::Array::New(env, dd.bNumConfigurations);
	for(uint8_t i = 0; i < dd.bNumConfigurations; i++){
		libusb_get_config_descriptor(device, i, &cdesc);
		v8cdescriptors.Set(i, Device::cdesc2V8(env, cdesc));
		libusb_free_config_descriptor(cdesc);
	}
	return v8cdescriptors;
}

Napi::Value Device::GetParent(const Napi::CallbackInfo& info){
	ENTER_METHOD(Device, 0);
	libusb_device* dev = libusb_get_parent(self->device);
	if(dev)
		return Device::get(env, dev);
	else
		return env.Null();
}

Napi::Value Device::Open(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 0);
	if (!self->device_handle){
		CHECK_USB(libusb_open(self->device, &self->device_handle));
	}
	return env.Undefined();
}

Napi::Value Device::Close(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 0);
	if (self->canClose()){
		libusb_close(self->device_handle);
		self->device_handle = NULL;
	}else{
		THROW_ERROR("Can't close device with a pending request");
	}
	return env.Undefined();
}

struct Req{
	uv_work_t req;
	Device* device;
	Napi::FunctionReference callback;
	int errcode;

	void submit(Device* d, Napi::Function cb, uv_work_cb backend, uv_work_cb after){
		callback.Reset(cb);
		device = d;
		device->ref();
		req.data = this;
		uv_queue_work(uv_default_loop(), &req, backend, (uv_after_work_cb) after);
	}

	static void default_after(uv_work_t *req){
		auto baton = (Req*)req->data;
		auto device = baton->device->Value();
		auto env = device.Env();
		Napi::HandleScope scope(env);

		baton->device->unref();

		if (!baton->callback.IsEmpty()) {
			Napi::Value error = env.Undefined();
			if (baton->errcode < 0){
				error = libusbException(env, baton->errcode).Value();
			}
			try {
				baton->callback.MakeCallback(device, { error });
			}
			catch (const Napi::Error& e) {
				Napi::Error::Fatal("", e.what());
			}
			baton->callback.Reset();
		}
		delete baton;
	}
};

struct Device_Reset: Req{
	static Napi::Value begin(const Napi::CallbackInfo& info) {
		Napi::Env env = info.Env(); 
		Napi::HandleScope scope(env);
		auto self = Napi::ObjectWrap<Device>::Unwrap(info.This().As<Napi::Object>());
		CHECK_OPEN();
		CALLBACK_ARG(0);
		auto baton = new Device_Reset;
		baton->submit(self, callback, &backend, &default_after);
		return env.Undefined();
	}

	static void backend(uv_work_t *req){
		auto baton = (Device_Reset*) req->data;
		baton->errcode = libusb_reset_device(baton->device->device_handle);
	}
};

struct Device_Clear_Halt: Req{
	int endpoint;

    static Napi::Value begin(const Napi::CallbackInfo& info) {
		int endpoint;
		Napi::Env env = info.Env();
		Napi::HandleScope scope(env);
		auto self = Napi::ObjectWrap<Device>::Unwrap(info.This().As<Napi::Object>());
		CHECK_OPEN();
		INT_ARG(endpoint, 0);
		CALLBACK_ARG(1);
		auto baton = new Device_Clear_Halt;
		baton->endpoint = endpoint;
                baton->submit(self, callback, &backend, &default_after);
                return env.Undefined();
        }

        static void backend(uv_work_t *req){
                auto baton = (Device_Clear_Halt*) req->data;
                baton->errcode = libusb_clear_halt(baton->device->device_handle, baton->endpoint);
        }
};



Napi::Value Device::IsKernelDriverActive(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 1);
	CHECK_OPEN();
	int interface;
	INT_ARG(interface, 0);
	int r = libusb_kernel_driver_active(self->device_handle, interface);
	CHECK_USB(r);
	return Napi::Boolean::New(env, r);
}

Napi::Value Device::DetachKernelDriver(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 1);
	CHECK_OPEN();
	int interface;
	INT_ARG(interface, 0);
	CHECK_USB(libusb_detach_kernel_driver(self->device_handle, interface));
	return env.Undefined();
}

Napi::Value Device::AttachKernelDriver(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 1);
	CHECK_OPEN();
	int interface;
	INT_ARG(interface, 0);
	CHECK_USB(libusb_attach_kernel_driver(self->device_handle, interface));
	return env.Undefined();
}

Napi::Value Device::ClaimInterface(const Napi::CallbackInfo& info) {
	ENTER_METHOD(Device, 1);
	CHECK_OPEN();
	int interface;
	INT_ARG(interface, 0);
	CHECK_USB(libusb_claim_interface(self->device_handle, interface));
	return env.Undefined();
}

struct Device_ReleaseInterface: Req{
	int interface;

	static Napi::Value begin(const Napi::CallbackInfo& info){
		Napi::Env env = info.Env();
		Napi::HandleScope scope(env);
		auto self = Napi::ObjectWrap<Device>::Unwrap(info.This().As<Napi::Object>());
		CHECK_OPEN();
		int interface;
		INT_ARG(interface, 0);
		CALLBACK_ARG(1);
		auto baton = new Device_ReleaseInterface;
		baton->interface = interface;
		baton->submit(self, callback, &backend, &default_after);

		return env.Undefined();
	}

	static void backend(uv_work_t *req){
		auto baton = (Device_ReleaseInterface*) req->data;
		baton->errcode = libusb_release_interface(baton->device->device_handle, baton->interface);
	}
};

struct Device_SetInterface: Req{
	int interface;
	int altsetting;

	static Napi::Value begin(const Napi::CallbackInfo& info){
		Napi::Env env = info.Env();
		Napi::HandleScope scope(env);
		auto self = Napi::ObjectWrap<Device>::Unwrap(info.This().As<Napi::Object>());
		CHECK_OPEN();
		int interface, altsetting;
		INT_ARG(interface, 0);
		INT_ARG(altsetting, 1);
		CALLBACK_ARG(2);
		auto baton = new Device_SetInterface;
		baton->interface = interface;
		baton->altsetting = altsetting;
		baton->submit(self, callback, &backend, &default_after);
		return env.Undefined();
	}

	static void backend(uv_work_t *req){
		auto baton = (Device_SetInterface*) req->data;
		baton->errcode = libusb_set_interface_alt_setting(
			baton->device->device_handle, baton->interface, baton->altsetting);
	}
};

struct Device_SetConfiguration: Req{
	int desired;

	static Napi::Value begin(const Napi::CallbackInfo& info){
		Napi::Env env = info.Env();
		Napi::HandleScope scope(env);
		auto self = Napi::ObjectWrap<Device>::Unwrap(info.This().As<Napi::Object>());
		CHECK_OPEN();
		int desired;
		INT_ARG(desired, 0);
		CALLBACK_ARG(1);
		auto baton = new Device_SetConfiguration;
		baton->desired = desired;
		baton->submit(self, callback, &backend, &default_after);
		return env.Undefined();
	}

	static void backend(uv_work_t *req){
		auto baton = (Device_SetConfiguration*) req->data;
		baton->errcode = libusb_set_configuration(
			baton->device->device_handle, baton->desired);
	}
};

Napi::Object Device::Init(Napi::Env env, Napi::Object exports) {
	auto func = Device::DefineClass(
		env,
		"Device",
		{
			Device::InstanceMethod("__getParent", &Device::GetParent),
			Device::InstanceMethod("__getConfigDescriptor", &Device::GetConfigDescriptor),
			Device::InstanceMethod("__getAllConfigDescriptors", &Device::GetAllConfigDescriptors),
			Device::InstanceMethod("__open", &Device::Open),
			Device::InstanceMethod("__close", &Device::Close),
			Device::StaticMethod("__clearHalt", Device_Clear_Halt::begin),
			Device::StaticMethod("reset", Device_Reset::begin),
			Device::InstanceMethod("__claimInterface", &Device::ClaimInterface),
			Device::StaticMethod("__releaseInterface", Device_ReleaseInterface::begin),
			Device::StaticMethod("__setInterface", Device_SetInterface::begin),
			Device::StaticMethod("__setConfiguration", Device_SetConfiguration::begin),
			Device::InstanceMethod("__isKernelDriverActive", &Device::IsKernelDriverActive),
			Device::InstanceMethod("__detachKernelDriver", &Device::DetachKernelDriver),
			Device::InstanceMethod("__attachKernelDriver", &Device::AttachKernelDriver),
		});
	exports.Set("Device", func);

	Device::constructor = Napi::Persistent(func);
	Device::constructor.SuppressDestruct();

	return exports;
}

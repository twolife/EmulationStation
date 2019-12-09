#include "platform.h"

#include <SDL_events.h>
#ifdef WIN32
#include <codecvt>
#else
#include <unistd.h>
#endif
#include <fcntl.h>

#include "Log.h"

#ifdef USE_DBUS
#include <dbus/dbus.h>
#define LOGIND_DEST  "org.freedesktop.login1"
#define LOGIND_PATH  "/org/freedesktop/login1"
#define LOGIND_IFACE "org.freedesktop.login1.Manager"

bool HasLogind()
{
	// recommended method by systemd devs. The seats directory
	// doesn't exist unless logind created it and therefore is running.
	// see also https://mail.gnome.org/archives/desktop-devel-list/2013-March/msg00092.html
	return (access("/run/systemd/seats/", F_OK) >= 0);
}

DBusMessage *DBusSystemSend(DBusMessage *message)
{
	DBusError error;
	dbus_error_init (&error);
	DBusConnection *con = dbus_bus_get(DBUS_BUS_SYSTEM, &error);

	DBusMessage *returnMessage = dbus_connection_send_with_reply_and_block(con, message, -1, &error);

	if (dbus_error_is_set(&error))
		LOG(LogError) << "DBus: Error " << error.name << " - " << error.message;

	dbus_error_free (&error);
	dbus_connection_unref(con);

	return returnMessage;
}

bool LogindCheckCapability(const char *capability)
{
	DBusMessage *message = dbus_message_new_method_call(LOGIND_DEST, LOGIND_PATH, LOGIND_IFACE, capability);
	DBusMessage *reply = DBusSystemSend(message);
	char *arg;

	if(reply && dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID))
	{
		// Returns one of "yes", "no" or "challenge". If "challenge" is
		// returned the operation is available, but only after authorization.
		return (strcmp(arg, "yes") == 0);
	}
	return false;
}

bool LogindSetPowerState(const char *state)
{
	DBusMessage *message = dbus_message_new_method_call(LOGIND_DEST, LOGIND_PATH, LOGIND_IFACE, state);

	// The user_interaction boolean parameters can be used to control
	// wether PolicyKit should interactively ask the user for authentication
	// credentials if it needs to.
	DBusMessageIter args;
	dbus_bool_t arg = false;
	dbus_message_iter_init_append(message, &args);
	dbus_message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &arg);

	return DBusSystemSend(message) != NULL;
}
#endif

int runShutdownCommand()
{
#ifdef WIN32 // windows
	return system("shutdown -s -t 0");
#else // osx / linux
#ifdef USE_DBUS
	if(HasLogind() && LogindCheckCapability("CanPowerOff")) {
		LOG(LogInfo) << "LogindSetPowerState('PowerOff')";
		Log::flush();
		return LogindSetPowerState("PowerOff");
	} else
#endif
	return system("sudo shutdown -h now");
#endif
}

int runRestartCommand()
{
#ifdef WIN32 // windows
	return system("shutdown -r -t 0");
#else // osx / linux
#ifdef USE_DBUS
	if(HasLogind() && LogindCheckCapability("CanReboot")) {
		LOG(LogInfo) << "LogindSetPowerState('Reboot')";
		Log::flush();
		return LogindSetPowerState("Reboot");
	} else
#endif
	return system("sudo shutdown -r now");
#endif
}

int runSystemCommand(const std::string& cmd_utf8)
{
#ifdef WIN32
	// on Windows we use _wsystem to support non-ASCII paths
	// which requires converting from utf8 to a wstring
	typedef std::codecvt_utf8<wchar_t> convert_type;
	std::wstring_convert<convert_type, wchar_t> converter;
	std::wstring wchar_str = converter.from_bytes(cmd_utf8);
	return _wsystem(wchar_str.c_str());
#else
	return system(cmd_utf8.c_str());
#endif
}

QuitMode quitMode = QuitMode::QUIT;

int quitES(QuitMode mode)
{
	quitMode = mode;

	SDL_Event *quit = new SDL_Event();
	quit->type = SDL_QUIT;
	SDL_PushEvent(quit);
	return 0;
}

void touch(const std::string& filename)
{
#ifdef WIN32
	FILE* fp = fopen(filename.c_str(), "ab+");
	if (fp != NULL)
		fclose(fp);
#else
	int fd = open(filename.c_str(), O_CREAT|O_WRONLY, 0644);
	if (fd >= 0)
		close(fd);
#endif
}

void processQuitMode()
{
	switch (quitMode)
	{
	case QuitMode::RESTART:
		LOG(LogInfo) << "Restarting EmulationStation";
		touch("/tmp/es-restart");
		break;
	case QuitMode::REBOOT:
		LOG(LogInfo) << "Rebooting system";
		touch("/tmp/es-sysrestart");
		runRestartCommand();
		break;
	case QuitMode::SHUTDOWN:
		LOG(LogInfo) << "Shutting system down";
		touch("/tmp/es-shutdown");
		runShutdownCommand();
		break;
	}
}

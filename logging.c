#include "logging.h"
#include "config.h"


static void log_handler(const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data) {

	openlog("actimedium", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_SYSLOG);
	syslog(LOG_ERR, "**ERROR** %s", message);
	closelog();

}

static void log_handler_debug(const gchar *log_domain,
		GLogLevelFlags log_level, const gchar *message, gpointer user_data) {

	if (DEBUG) {
		openlog("actimedium", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_SYSLOG);
		syslog(LOG_DEBUG, "**DEBUG** %s", message);
		closelog();
	}
}

void init_logging() {
	g_log_set_default_handler(log_handler, NULL);
	g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO
			| G_LOG_LEVEL_MESSAGE, log_handler_debug, NULL);
}

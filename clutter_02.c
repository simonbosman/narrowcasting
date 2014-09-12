#include <clutter/clutter.h>
#include <clutter-gst/clutter-gst.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mx/mx.h>
#include "logging.h"
#include "config.h"
#include "pinpoint.h"
#include "get_mac.h"

gulong stage_handler;
static gboolean fullscreen = FALSE;
static gboolean onscreen = FALSE;
static gboolean first_pp_run = TRUE;
static MxStyle *style;
static ClutterTimeline *pp_timeline;

static GOptionEntry entries[] = { { "fullscreen", 'f', 0, G_OPTION_ARG_NONE,
		&fullscreen, "Start in fullscreen mode", NULL }, { "onscreen", 'o', 0,
		G_OPTION_ARG_NONE, &onscreen, "Show messages on screen", NULL },
		{ NULL } };

static void create_button(ClutterContainer *parent, ClutterActor *button,
		const gchar *name, const gchar *text, gint x, gint y, MxStyle *style,
		gint width, gint height) {

	clutter_actor_set_name(CLUTTER_ACTOR(button), name);
	mx_stylable_set_style(MX_STYLABLE(button), style);
	clutter_container_add_actor(CLUTTER_CONTAINER(parent),
			CLUTTER_ACTOR(button));
	clutter_actor_set_position(CLUTTER_ACTOR(button), x, y);

	if (width || height)
		clutter_actor_set_size(CLUTTER_ACTOR(button), width, height);

}

void grid_show() {
	GList *stageContainers = clutter_container_get_children(CLUTTER_CONTAINER(
			clutter_stage_get_default()));

	guint i;
	for (i = 0; i < g_list_length(stageContainers); i++) {

		if (!CLUTTER_IS_ACTOR(g_list_nth_data(stageContainers, i)))
			break;

		const gchar *textName = clutter_actor_get_name(CLUTTER_ACTOR(
				g_list_nth_data(stageContainers, i)));

		if (textName == NULL)
			break;

		if (strcmp(textName, "pp_container") == 0) {
			clutter_actor_hide(CLUTTER_ACTOR(g_list_nth_data(
							stageContainers, i)));
		}

		//		if (strcmp(textName, "telephone") == 0) {
		//			clutter_actor_destroy(CLUTTER_ACTOR(g_list_nth_data(
		//							stageContainers, i)));
		//			break;
		//		}

		if (strcmp(textName, "grid_stage") == 0) {
			clutter_actor_show(CLUTTER_ACTOR(
					g_list_nth_data(stageContainers, i)));
		}

	}
	g_list_free(stageContainers);
	stageContainers = NULL;

}

static void release_text_timeline_completed(ClutterTimeline *timeline,
		gpointer text) {
	g_object_unref(timeline);
	clutter_actor_destroy(text);
	grid_show();
}

static void grid_timeline_completed(ClutterTimeline *timeline, gpointer text) {
	g_object_unref(timeline);
	pp_init(first_pp_run, clutter_actor_get_name(CLUTTER_ACTOR(text)));
	first_pp_run = FALSE;
	clutter_actor_destroy(text);
}

static void press_text_timeline_completed(ClutterTimeline *timeline,
		gpointer text) {

	g_object_unref(pp_timeline);
	static ClutterTimeline *grid_timeline;

	g_signal_handler_disconnect(clutter_stage_get_default(), stage_handler);
	stage_handler = 0;

	clutter_actor_animate(CLUTTER_ACTOR(text), CLUTTER_EASE_OUT_SINE, 300,
			"width", 0.0, "height", 0.0, "opacity", 0, "x", 960.0, "y", 540.0,
			NULL);

	grid_timeline = clutter_timeline_new(310);
	clutter_timeline_start(grid_timeline);
	g_signal_connect(grid_timeline, "completed", G_CALLBACK(grid_timeline_completed), CLUTTER_ACTOR(text));
}

static void release_text(ClutterActor *main_stage, ClutterEvent *event,
		gpointer text) {

	static ClutterTimeline *release_timeline;

	g_signal_handler_disconnect(clutter_stage_get_default(), stage_handler);
	stage_handler = 0;

	clutter_timeline_stop(pp_timeline);
	clutter_timeline_rewind(pp_timeline);

	clutter_actor_animate(CLUTTER_ACTOR(text), CLUTTER_EASE_OUT_SINE, 300,
			"width", 0.0, "height", 0.0, "opacity", 0, "x", 960.0, "y", 540.0,
			NULL);

	release_timeline = clutter_timeline_new(310);
	clutter_timeline_start(release_timeline);
	g_signal_connect(release_timeline, "completed", G_CALLBACK(release_text_timeline_completed), CLUTTER_ACTOR(text));
}

static void press_text(ClutterActor *text_org, ClutterEvent *event,
		gpointer user_data) {

	//ClutterActor *button;
	gfloat x, y;
	ClutterActor *text;
	//button = mx_button_new();
	//create_button(CLUTTER_CONTAINER(clutter_stage_get_default()), button,
	//		"telephone", "", 1750, 25, style, 128, 128);

	text = clutter_clone_new(CLUTTER_ACTOR(text_org));
	clutter_actor_set_name(CLUTTER_ACTOR(text),
			clutter_actor_get_name(text_org));

	pp_timeline = clutter_timeline_new(5000);
	g_signal_connect(pp_timeline, "completed", G_CALLBACK(press_text_timeline_completed), text);
	clutter_timeline_start(pp_timeline);

	clutter_actor_hide(clutter_actor_get_parent(text_org));
	stage_handler = g_signal_connect(clutter_stage_get_default(),
			"button-press-event", G_CALLBACK(release_text), text);
	clutter_actor_get_position(CLUTTER_ACTOR(text_org), &x, &y);
	clutter_actor_set_depth(CLUTTER_ACTOR(text), -10.0);
	clutter_container_add_actor(CLUTTER_CONTAINER(clutter_stage_get_default()),
			CLUTTER_ACTOR(text));
	clutter_actor_set_position(CLUTTER_ACTOR(text), x, y);
	clutter_actor_set_opacity(CLUTTER_ACTOR(text), 0);
	clutter_actor_animate(CLUTTER_ACTOR(text), CLUTTER_EASE_IN_SINE, 375,
			"width", 1920.0, "height", 1080.0, "opacity", 255, "x", 0.0, "y",
			0.0, NULL);
}

int main(int argc, char **argv) {

	//Simple check if macaddress is valid
	//TODO: put this in an self decrypting part of the binary
	char mac_addr[13];
	get_mac(mac_addr);

	if(strcmp(mac_addr, "00269EA0F5F7")){
		printf("   |\n");
		printf("  /S\\\n");
		printf(" /(0)\\\n");
		printf("/minos\\\n");
		return(EXIT_FAILURE);
	}

	const ClutterColor bgColor = { 0x00, 0x00, 0x00, 0xff };
	ClutterStage *stage;
	MxGrid *grid;
	ClutterActor *textCel[32];
	GError *options_error = NULL;
	GError *style_error = NULL;
	GError *text_error = NULL;
	GOptionContext *context = NULL;
	int i;
	gchar file_name[10];
	gchar pres_name[25];
	const gchar *folder = "img/";
	const gchar *folder_pin = "presentations/";

	context = g_option_context_new("- Actimedium");
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_add_group(context, clutter_get_option_group_without_init());
	g_option_context_add_group(context, cogl_get_option_group());
	if (!g_option_context_parse(context, &argc, &argv, &options_error)) {
		g_error("Hey joe, loading startup settings failed, %s",
				options_error->message);
		g_error_free(options_error);
		options_error = NULL;
		return EXIT_FAILURE;
	}
	g_option_context_free(context);

	if (!onscreen)
		init_logging();

#ifdef USE_CLUTTER_GST
	g_debug("Starting Actimedium with gst support");
	clutter_gst_init(&argc, &argv);
#else
	g_debug("Starting Actimedium");
	clutter_init (&argc, &argv);
#endif

	stage = CLUTTER_STAGE(clutter_stage_get_default());
	clutter_actor_set_name(CLUTTER_ACTOR(stage), "main_stage");
	clutter_stage_hide_cursor(CLUTTER_STAGE(stage));
	clutter_stage_set_color(CLUTTER_STAGE(stage), &bgColor);
	fullscreen ? clutter_stage_set_fullscreen(CLUTTER_STAGE(stage), TRUE)
			: clutter_actor_set_size(CLUTTER_ACTOR(stage), 1280, 960);

	style = mx_style_new();

	if (!mx_style_load_from_file(style, "style/default.css", &style_error)) {
		g_error("loading css failed, %s", style_error->message);
		g_error_free(style_error);
		style_error = NULL;
	}

	grid = MX_GRID(mx_grid_new());
	clutter_actor_set_name(CLUTTER_ACTOR(grid), "grid_stage");
	clutter_actor_set_position(CLUTTER_ACTOR(grid), 20.0, 10.0);
	mx_grid_set_column_spacing(MX_GRID(grid), 15.0);
	mx_grid_set_row_spacing(MX_GRID(grid), 15.0);
	mx_grid_set_orientation(MX_GRID(grid), MX_ORIENTATION_HORIZONTAL);
	mx_grid_set_max_stride(MX_GRID(grid), 6);

	for (i = 1; i < 31; i++) {

		g_snprintf(file_name, 10, "%d.jpg", i);
		g_snprintf(pres_name, 25, "%s%i.pin", folder_pin, i);

		gchar *dn = (gchar *) calloc(strlen(folder) + strlen(file_name) + 1,
				sizeof(gchar));
		strcat(dn, folder);
		strcat(dn, file_name);
		textCel[i] = clutter_texture_new_from_file(dn, &text_error);
		free(dn);
		dn = NULL;

		if (text_error) {
			g_critical("oops, creating texture cells resulted in an error, %s",
					text_error->message);
			g_error_free(text_error);
			text_error = NULL;
			continue;
		}

		clutter_actor_set_reactive(textCel[i], TRUE);
		clutter_actor_set_size(textCel[i], 300, 200);
		clutter_actor_set_name(textCel[i], pres_name);
		clutter_container_add_actor(CLUTTER_CONTAINER(grid), textCel[i]);

		g_signal_connect(textCel[i], "button-press-event",
				G_CALLBACK(press_text), NULL);

	}

	clutter_container_add_actor(CLUTTER_CONTAINER(stage), CLUTTER_ACTOR(grid));
	clutter_actor_show(CLUTTER_ACTOR(stage));
	clutter_main();

	return (EXIT_SUCCESS);
}


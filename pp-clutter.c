#include "pinpoint.h"
#include <clutter/x11/clutter-x11.h>
#include <gio/gio.h>
#include <clutter-gst/clutter-gst.h>
#ifdef USE_DAX
#include <dax/dax.h>
#include "pp-super-aa.h"
#endif
#include <stdlib.h>
#define PKGDATADIR "/usr/share/pinpoint"

/* #define QUICK_ACCESS_LEFT - uncomment to move speed access from top to left,
 *                             useful on meego netbook
 */

#define RESTDEPTH   -9000.0
#define RESTX        4600.0
#define STARTPOS    -3000.0

static ClutterColor black = { 0x00, 0x00, 0x00, 0xff };
static gulong key_press_event;
static ClutterTimeline *slide_timeline;
static gboolean first_run;

typedef struct _ClutterRenderer {
	PinPointRenderer renderer;
	GHashTable *bg_cache; /* only load the same backgrounds once */
	ClutterActor *stage;
	ClutterActor *background;
	ClutterActor *midground;
	ClutterActor *shading;
	ClutterActor *foreground;
	ClutterActor *json_layer;
	char *path; /* path of the file of the GFileMonitor callback */
	float rest_y; /* where the text can rest */
} ClutterRenderer;

typedef struct {
	PinPointRenderer *renderer;
	ClutterActor *background;
	ClutterActor *text;
	float rest_y; /* y coordinate when text is stationary unused */

	ClutterState *state;
	ClutterActor *json_slide;
	ClutterActor *background2;
	ClutterScript *script;
	ClutterActor *midground;
	ClutterActor *foreground;
	ClutterActor *shading;
} ClutterPointData;

#define CLUTTER_RENDERER(renderer)  ((ClutterRenderer *) renderer)

static void slide_timeline_completed(ClutterTimeline *timeline,
		gpointer user_data);
static void leave_slide(ClutterRenderer *renderer, gboolean backwards);
static void show_slide(ClutterRenderer *renderer, gboolean backwards);
static void file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
		GFileMonitorEvent event_type, ClutterRenderer *renderer);
gboolean key_pressed(ClutterActor *actor, ClutterEvent *event,
		ClutterRenderer *renderer);

static void _destroy_surface(gpointer data) {
	/* not destroying background, since it would be destroyed with
	 * the stage itself.
	 */
}

static void clutter_render_init_partial(PinPointRenderer *pp_renderer) {

	ClutterRenderer *renderer = CLUTTER_RENDERER (pp_renderer);
	GList *s;

	key_press_event
			= g_signal_connect (clutter_stage_get_default(), "key-press-event",
					G_CALLBACK (key_pressed), renderer);

	for (s = pp_slides; s; s = s->next) {

		PinPointPoint *point;
		ClutterPointData *data;
		point = s->data;
		data = point->data;

		if (data->background)
			clutter_actor_show(data->background);
		if (data->text)
			clutter_actor_show(data->text);
		if (data->json_slide)
			clutter_actor_show(data->json_slide);
	}

}

static void clutter_renderer_init(PinPointRenderer *pp_renderer,
		char *pinpoint_file) {
	ClutterRenderer *renderer = CLUTTER_RENDERER (pp_renderer);
	GFileMonitor *monitor;
	ClutterContainer *pp_container = CLUTTER_CONTAINER(clutter_group_new());
	clutter_actor_set_name(pp_container, "pp_container");

	renderer->rest_y = STARTPOS;
	renderer->background = clutter_group_new();
	renderer->midground = clutter_group_new();
	renderer->foreground = clutter_group_new();
	renderer->json_layer = clutter_group_new();
	renderer->shading = clutter_rectangle_new_with_color(&black);
	clutter_actor_set_opacity(renderer->shading, 0x77);

	clutter_container_add_actor(CLUTTER_CONTAINER (renderer->midground),
			renderer->shading);
	clutter_container_add(pp_container, renderer->background,
			renderer->midground, renderer->foreground, renderer->json_layer, NULL);
	clutter_container_add_actor(CLUTTER_CONTAINER(clutter_stage_get_default()), CLUTTER_ACTOR(pp_container));

	key_press_event
			= g_signal_connect (clutter_stage_get_default(), "key-press-event",
					G_CALLBACK (key_pressed), renderer);

	renderer->path = pinpoint_file;

	renderer->bg_cache = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
			_destroy_surface);
}

static void clutter_renderer_run(PinPointRenderer *renderer) {
	first_run = TRUE;
	show_slide(CLUTTER_RENDERER (renderer), FALSE);
	slide_timeline = clutter_timeline_new(5000);
	clutter_timeline_start(slide_timeline);
	g_signal_connect(slide_timeline, "completed", slide_timeline_completed, CLUTTER_RENDERER(renderer));
}

static void clutter_renderer_finalize(PinPointRenderer *pp_renderer) {
	ClutterRenderer *renderer = CLUTTER_RENDERER (pp_renderer);
	GList *s;

	g_signal_handler_disconnect(clutter_stage_get_default(), key_press_event);
	g_object_unref(slide_timeline);

	if (pp_slidep) {
		leave_slide(renderer, FALSE);
	}
	pp_slidep = g_list_nth(pp_slides, g_list_length(0));

	for (s = g_list_first(pp_slides); s; s = s->next) {

		PinPointPoint *point;
		ClutterPointData *data;
		point = s->data;
		data = point->data;

		if (data->background)
			clutter_actor_hide(data->background);
		if (data->text)
			clutter_actor_hide(data->text);
		if (data->json_slide)
			clutter_actor_hide(data->json_slide);
	}

}

static ClutterActor *
_clutter_get_texture(ClutterRenderer *renderer, const char *file) {
	ClutterActor *source = NULL;

	if (!strcmp(file, "NULL"))
		return NULL;

	if (renderer->bg_cache)
		source = g_hash_table_lookup(renderer->bg_cache, (char *) file);

	if (source) {
		return clutter_clone_new(source);
	}

	GError *error = NULL;
	source = clutter_texture_new_from_file(file, &error);

	if (error) {
		g_critical("Creating textures from file %s resulted in an error %s",
				file, error->message);
		g_error_free(error);
		error = NULL;
	}

	if (!source)
		return NULL;

	clutter_container_add_actor(
			CLUTTER_CONTAINER (clutter_stage_get_default()), source);
	clutter_actor_hide(source);

	if (renderer->bg_cache)
		g_hash_table_insert(renderer->bg_cache, (char *) file, source);

	return clutter_clone_new(source);
}

static gboolean clutter_renderer_make_point(PinPointRenderer *pp_renderer,
		PinPointPoint *point) {
	ClutterRenderer *renderer = CLUTTER_RENDERER (pp_renderer);
	ClutterPointData *data = point->data;
	ClutterColor color;
	gboolean ret;

	switch (point->bg_type) {
	case PP_BG_COLOR: {
		ret = clutter_color_from_string(&color, point->bg);
		if (ret)
			data->background = g_object_new(CLUTTER_TYPE_RECTANGLE, "color",
					&color, "width", 100.0, "height", 100.0, NULL);
	}
		break;
	case PP_BG_IMAGE:
		data->background = _clutter_get_texture(renderer, point->bg);
		ret = TRUE;
		break;
	case PP_BG_VIDEO:
		data->background = clutter_gst_video_texture_new();
		clutter_media_set_filename(CLUTTER_MEDIA (data->background), point->bg);
		/* should pre-roll the video and set the size */
		clutter_actor_set_size(data->background, 400, 300);
		ret = TRUE;
		break;
	default:
		g_assert_not_reached();
	}

	if (renderer->background) {
		clutter_container_add_actor(CLUTTER_CONTAINER (renderer->background),
				data->background);
		clutter_actor_set_opacity(data->background, 0);
		clutter_actor_animate(CLUTTER_ACTOR(data->background),
				CLUTTER_EASE_IN_SINE, 375, "width", 500.0, "height", 1080.0,
				"opacity", 255, "x", 0.0, "y", 0.0, NULL);
	}

	clutter_color_from_string(&color, point->text_color);

	if (point->use_markup) {
		data->text = g_object_new(CLUTTER_TYPE_TEXT, "font-name", point->font,
				"text", point->text, "line-alignment", point->text_align,
				"color", &color, "use-markup", TRUE, NULL);
	} else {

		data->text = clutter_text_new_full("font-name", point->text, &color);
		clutter_text_set_line_alignment(data->text, point->text_align);
	}

	if (!renderer->foreground)
		return ret;

	clutter_container_add_actor(CLUTTER_CONTAINER (renderer->foreground),
			data->text);

	clutter_actor_set_position(data->text, RESTX, renderer->rest_y);
	data->rest_y = renderer->rest_y;
	renderer->rest_y += clutter_actor_get_height(data->text);
	clutter_actor_set_depth(data->text, RESTDEPTH);

	return ret;
}

static void *
clutter_renderer_allocate_data(PinPointRenderer *renderer) {
	ClutterPointData *data = g_slice_new0 (ClutterPointData);
	data->renderer = renderer;
	return data;
}

static void clutter_renderer_free_data(PinPointRenderer *renderer, void *datap) {
	ClutterPointData *data = datap;

	if (data->background)
		clutter_actor_destroy(data->background);
	if (data->text)
		clutter_actor_destroy(data->text);
	if (data->json_slide)
		clutter_actor_destroy(data->json_slide);

}

static void slide_timeline_completed(ClutterTimeline *timeline,
		gpointer renderer) {
	clutter_timeline_rewind(timeline);
	clutter_timeline_start(timeline);

	if (first_run) {
		if (pp_slidep) {
			pp_slidep = g_list_first(pp_slidep);
			leave_slide(renderer, FALSE);
			pp_slidep = pp_slidep->next;
			show_slide(renderer, FALSE);
		}
		first_run = FALSE;
		return;
	}

	if (pp_slidep && pp_slidep->next) {
		leave_slide(renderer, FALSE);
		pp_slidep = pp_slidep->next;
		show_slide(renderer, FALSE);
	} else {
		pp_stop();
	}
}

gboolean key_pressed(ClutterActor *actor, ClutterEvent *event,
		ClutterRenderer *renderer) {

	if (event) /* There is no event for the first triggering */
		switch (clutter_event_get_key_symbol(event)) {
		case CLUTTER_Left:
		case CLUTTER_Up:
			if (pp_slidep && pp_slidep->prev) {
				leave_slide(renderer, TRUE);
				pp_slidep = pp_slidep->prev;
				show_slide(renderer, TRUE);
			}
			break;
		case CLUTTER_BackSpace:
		case CLUTTER_Prior:
			if (pp_slidep && pp_slidep->prev) {
				leave_slide(renderer, TRUE);
				pp_slidep = pp_slidep->prev;
				show_slide(renderer, TRUE);
			}
			break;
		case CLUTTER_Right:
		case CLUTTER_space:
		case CLUTTER_Next:
		case CLUTTER_Down:
			if (pp_slidep && pp_slidep->next) {
				leave_slide(renderer, FALSE);
				pp_slidep = pp_slidep->next;
				show_slide(renderer, FALSE);
			}
			break;
		case CLUTTER_Escape:
			pp_stop();
			break;
		}
	return TRUE;
}

static void leave_slide(ClutterRenderer *renderer, gboolean backwards) {
	PinPointPoint *point = pp_slidep->data;
	ClutterPointData *data = point->data;

	if (CLUTTER_GST_IS_VIDEO_TEXTURE (data->background)) {
		clutter_media_set_playing(CLUTTER_MEDIA (data->background), FALSE);
	}
	if (data->script) {
		if (backwards)
			clutter_state_set_state(data->state, "pre");
		else
			clutter_state_set_state(data->state, "post");
	}
}

static void state_completed(ClutterState *state, gpointer user_data) {
	PinPointPoint *point = user_data;
	ClutterPointData *data = point->data;
	const gchar *new_state = clutter_state_get_state(state);

	if (new_state == g_intern_static_string("post") || new_state
			== g_intern_static_string("pre")) {
		clutter_actor_hide(data->json_slide);
		if (data->background2) {
			clutter_actor_reparent(data->text,
					CLUTTER_RENDERER (data->renderer)->foreground);

			g_object_set(data->text, "depth", RESTDEPTH, "scale-x", 1.0,
					"scale-y", 1.0, "x", RESTX, "y", data->rest_y, NULL);
			if (data->background)
				clutter_actor_set_opacity(data->background, 0);
		}
	}
}

static gchar *pp_lookup_transition(const gchar *transition) {
	int i;
	gchar *dirs[] = { "", "./transitions/", PKGDATADIR, NULL };
	for (i = 0; dirs[i]; i++) {
		gchar *path = g_strdup_printf("%s%s.json", dirs[i], transition);
		if (g_file_test(path, G_FILE_TEST_EXISTS))
			return path;
		g_free(path);
	}
	return NULL;
}

static void show_slide(ClutterRenderer *renderer, gboolean backwards) {

	PinPointPoint *point;
	ClutterPointData *data;
	ClutterColor color;

	if (!pp_slidep)
		return;

	point = pp_slidep->data;
	data = point->data;

	if (point->stage_color) {
		clutter_color_from_string(&color, point->stage_color);
		clutter_stage_set_color(CLUTTER_STAGE(clutter_stage_get_default()),
				&color);
	}

	if (data->background) {
		float bg_x, bg_y, bg_width, bg_height, bg_scale;
		if (CLUTTER_IS_RECTANGLE (data->background)) {
			clutter_actor_get_size(clutter_stage_get_default(), &bg_width,
					&bg_height);
			clutter_actor_set_size(data->background, bg_width, bg_height);
		} else {
			clutter_actor_get_size(data->background, &bg_width, &bg_height);
		}

		pp_get_background_position_scale(point, clutter_actor_get_width(
				clutter_stage_get_default()), clutter_actor_get_height(
				clutter_stage_get_default()), bg_width, bg_height, &bg_x,
				&bg_y, &bg_scale);

		clutter_actor_set_scale(data->background, bg_scale, bg_scale);
		clutter_actor_set_position(data->background, bg_x, bg_y);

		if (CLUTTER_GST_IS_VIDEO_TEXTURE (data->background)) {
			clutter_media_set_progress(CLUTTER_MEDIA (data->background), 0.40);
			clutter_media_set_playing(CLUTTER_MEDIA (data->background), TRUE);
		}
	}

	GError *error = NULL;
	clutter_actor_animate(renderer->foreground, CLUTTER_LINEAR, 500, "opacity",
			0, NULL);
	clutter_actor_animate(renderer->midground, CLUTTER_LINEAR, 500, "opacity",
			0, NULL);
	clutter_actor_animate(renderer->background, CLUTTER_LINEAR, 500, "opacity",
			0, NULL);
	if (!data->script) {
		gchar *path = pp_lookup_transition(point->transition);
		data->script = clutter_script_new();
		clutter_script_load_from_file(data->script, path, &error);
		g_free(path);
		data->foreground
				= CLUTTER_ACTOR (clutter_script_get_object (data->script, "foreground"));
		data->midground
				= CLUTTER_ACTOR (clutter_script_get_object (data->script, "midground"));
		data->background2
				= CLUTTER_ACTOR (clutter_script_get_object (data->script, "background"));
		data->state
				= CLUTTER_STATE (clutter_script_get_object (data->script, "state"));
		data->json_slide
				= CLUTTER_ACTOR (clutter_script_get_object (data->script, "actor"));
		clutter_container_add_actor(CLUTTER_CONTAINER (renderer->json_layer),
				data->json_slide);
		g_signal_connect (data->state, "completed", G_CALLBACK (state_completed), point);
		clutter_state_warp_to_state(data->state, "pre");

		if (data->background2) /* parmanently steal background */
		{
			if (data->background)
				clutter_actor_reparent(data->background, data->background2);
		}
		clutter_actor_set_size(data->json_slide, clutter_actor_get_width(
				clutter_stage_get_default()), clutter_actor_get_height(
				clutter_stage_get_default()));

		clutter_actor_set_size(data->foreground, clutter_actor_get_width(
				clutter_stage_get_default()), clutter_actor_get_height(
				clutter_stage_get_default()));

		clutter_actor_set_size(data->background2, clutter_actor_get_width(
				clutter_stage_get_default()), clutter_actor_get_height(
				clutter_stage_get_default()));
		if (!data->json_slide) {
			g_warning("failed to load transition %s %s\n", point->transition,
					error ? error->message : "");
			return;
		}
		if (data->foreground) {
			clutter_actor_reparent(data->text, data->foreground);
		}
		if (data->background)
			clutter_actor_set_opacity(data->background, 255);

		{
			float text_x, text_y, text_width, text_height, text_scale;

			clutter_actor_get_size(data->text, &text_width, &text_height);
			pp_get_text_position_scale(point, clutter_actor_get_width(
					clutter_stage_get_default()), clutter_actor_get_height(
					clutter_stage_get_default()), text_width, text_height,
					&text_x, &text_y, &text_scale);
			g_object_set(data->text, "depth", 0.0, "scale-x", text_scale,
					"scale-y", text_scale, "x", text_x, "y", text_y, NULL);

			if (clutter_actor_get_width(data->text) > 1.0) {
				ClutterColor color;
				float shading_x, shading_y, shading_width, shading_height;
				clutter_color_from_string(&color, point->shading_color);

				pp_get_shading_position_size(clutter_actor_get_width(
						clutter_stage_get_default()), clutter_actor_get_height(
						clutter_stage_get_default()), text_x, text_y,
						text_width, text_height, text_scale, &shading_x,
						&shading_y, &shading_width, &shading_height);
				if (!data->shading) {
					data->shading = clutter_rectangle_new_with_color(&black);
					clutter_container_add_actor(
							CLUTTER_CONTAINER (data->midground), data->shading);
					clutter_actor_set_size(
							data->midground,
							clutter_actor_get_width(clutter_stage_get_default()),
							clutter_actor_get_height(
									clutter_stage_get_default()));
				}
				g_object_set(data->shading, "depth", -0.01, "x", shading_x,
						"y", shading_y, "opacity",
						(int) (point->shading_opacity * 255), "color", &color,
						"width", shading_width, "height", shading_height, NULL);
			} else /* no text, fade out shading */
			if (data->shading)
				g_object_set(data->shading, "opacity", 0, NULL);
			if (data->foreground) {
				clutter_actor_reparent(data->text, data->foreground);
			}
		}

		if (!backwards)
			clutter_actor_raise_top(data->json_slide);
		clutter_actor_show(data->json_slide);
		clutter_state_set_state(data->state, "show");
	}

}

static guint reload_tag = 0;

static gboolean reload(gpointer data) {
	ClutterRenderer *renderer = data;
	char *text = NULL;

	if (!g_file_get_contents(renderer->path, &text, NULL, NULL))
		g_error("failed to load slides from %s\n", renderer->path);
	renderer->rest_y = STARTPOS;
	pp_parse_slides(PINPOINT_RENDERER (renderer), text);
	g_free(text);
	show_slide(renderer, FALSE);
	reload_tag = 0;
	return FALSE;
}

static void file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
		GFileMonitorEvent event_type, ClutterRenderer *renderer) {
	if (reload_tag)
		g_source_remove(reload_tag);
	reload_tag = g_timeout_add(200, reload, renderer);
}

static ClutterRenderer clutter_renderer_vtable = { .renderer = { .init =
		clutter_renderer_init, .run = clutter_renderer_run, .finalize =
		clutter_renderer_finalize, .make_point = clutter_renderer_make_point,
		.allocate_data = clutter_renderer_allocate_data, .free_data =
				clutter_renderer_free_data, .init_partial =
				clutter_render_init_partial } };

PinPointRenderer *pp_clutter_renderer(void) {
	return (void*) &clutter_renderer_vtable;
}

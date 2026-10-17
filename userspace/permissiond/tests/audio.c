#include <pipewire/pipewire.h>
#include <pipewire/extensions/client-node.h>
#include <spa/param/audio/format-utils.h>
#include <stdio.h>
#include <string.h>

struct probe {
    struct pw_main_loop *loop;
    struct pw_core *core;
    struct pw_stream *stream;
    struct pw_proxy *adapter, *capture;
    unsigned int flags, buffers, sink;
    int tick;
};

static void global(void *data, uint32_t id, uint32_t permissions, const char *type,
                   uint32_t version, const struct spa_dict *props)
{
    struct probe *p = data;
    const char *factory = spa_dict_lookup(props, PW_KEY_FACTORY_NAME);
    const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (factory && !strcmp(factory, "client-node")) p->flags |= 1;
    if (factory && (!strcmp(factory, "adapter") || !strcmp(factory, "spa-node-factory"))) p->flags |= 256;
    if (name && !strcmp(name, "qa-playback")) {
        p->sink = id;
        if (!(permissions & PW_PERM_W)) p->flags |= 2;
    }
    if (name && !strcmp(name, "qa-capture")) p->flags |= 512;
    const char *node = spa_dict_lookup(props, PW_KEY_NODE_ID);
    const char *direction = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
    if (node && p->sink && strtoul(node, NULL, 10) == p->sink && direction && !strcmp(direction, "out")) p->flags |= 1024;
}

static void error(void *data, uint32_t id, int seq, int res, const char *message)
{
    struct probe *p = data;
    fprintf(stderr, "PERMISSION_AUDIO error id=%u res=%d %s\n", id, res, message);
    if (res >= 0) return;
    if (p->adapter && id == pw_proxy_get_id(p->adapter)) p->flags |= 4;
    if (p->capture && id == pw_proxy_get_id(p->capture)) p->flags |= 8;
}

static void process(void *data)
{
    struct probe *p = data;
    struct pw_buffer *buffer = pw_stream_dequeue_buffer(p->stream);
    if (!buffer) return;
    struct spa_data *out = &buffer->buffer->datas[0];
    if (out->data && out->chunk) {
        memset(out->data, 0, out->maxsize);
        out->chunk->offset = 0;
        out->chunk->stride = 2 * sizeof(float);
        out->chunk->size = out->maxsize;
        ++p->buffers;
    }
    pw_stream_queue_buffer(p->stream, buffer);
}

static void tick(void *data, uint64_t count)
{
    struct probe *p = data;
    if (++p->tick == 3) {
        struct pw_properties *props = pw_properties_new(PW_KEY_SEC_UID, "0", NULL);
        pw_core_update_properties(p->core, &props->dict);
        pw_properties_free(props);
        props = pw_properties_new(PW_KEY_FACTORY_NAME, "support.null-audio-sink", PW_KEY_NODE_NAME, "unsafe-node", NULL);
        p->adapter = pw_core_create_object(p->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0);
        pw_properties_free(props);
        props = pw_properties_new(PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_CLASS, "Audio/Sink", NULL);
        p->capture = pw_core_create_object(p->core, "client-node", PW_TYPE_INTERFACE_ClientNode, PW_VERSION_CLIENT_NODE, &props->dict, 0);
        pw_properties_free(props);
    }
    if (p->tick == 10) pw_main_loop_quit(p->loop);
}

unsigned int permission_audio_probe(void)
{
    struct probe p = {0};
    struct spa_hook core_hook, registry_hook;
    const struct pw_core_events core_events = { PW_VERSION_CORE_EVENTS, .error = error };
    const struct pw_registry_events registry_events = { PW_VERSION_REGISTRY_EVENTS, .global = global };
    const struct pw_stream_events stream_events = { PW_VERSION_STREAM_EVENTS, .process = process };
    pw_init(NULL, NULL);
    p.loop = pw_main_loop_new(NULL);
    struct pw_context *context = pw_context_new(pw_main_loop_get_loop(p.loop), NULL, 0);
    p.core = pw_context_connect(context, NULL, 0);
    if (!p.core) goto done;
    pw_core_add_listener(p.core, &core_hook, &core_events, &p);
    struct pw_registry *registry = pw_core_get_registry(p.core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(registry, &registry_hook, &registry_events, &p);
    p.stream = pw_stream_new(p.core, "Permission playback test", pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_TARGET_OBJECT, "qa-playback", NULL));
    pw_stream_add_listener(p.stream, &(struct spa_hook){0}, &stream_events, &p);
    uint8_t bytes[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
    struct spa_audio_info_raw format = { .format = SPA_AUDIO_FORMAT_F32, .rate = 48000,
        .channels = 2, .position = { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR } };
    const struct spa_pod *params[] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
    pw_stream_connect(p.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1);
    struct spa_source *timer = pw_loop_add_timer(pw_main_loop_get_loop(p.loop), tick, &p);
    struct timespec interval = {.tv_nsec = 400000000};
    pw_loop_update_timer(pw_main_loop_get_loop(p.loop), timer, &interval, &interval, false);
    pw_main_loop_run(p.loop);
    pw_loop_destroy_source(pw_main_loop_get_loop(p.loop), timer);
    if (p.adapter) pw_proxy_destroy(p.adapter);
    if (p.capture) pw_proxy_destroy(p.capture);
    pw_stream_destroy(p.stream);
    pw_proxy_destroy((struct pw_proxy *)registry);
    pw_core_disconnect(p.core);
done:
    pw_context_destroy(context);
    pw_main_loop_destroy(p.loop);
    if (p.buffers) p.flags |= 16;
    fprintf(stderr, "PERMISSION_AUDIO flags=%u buffers=%u\n", p.flags, p.buffers);
    return p.flags;
}

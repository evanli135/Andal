#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "internal.h"

// ============================================================================
// EventStore Python type
// ============================================================================

typedef struct {
    PyObject_HEAD
    EventStore* store;
} PyEventStore;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

static PyObject* pystore_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
    PyEventStore* self = (PyEventStore*)type->tp_alloc(type, 0);
    if (self) self->store = NULL;
    return (PyObject*)self;
}

static int pystore_init(PyEventStore* self, PyObject* args, PyObject* kwargs) {
    const char* path;
    if (!PyArg_ParseTuple(args, "s", &path)) return -1;

    self->store = event_store_open(path);
    if (!self->store) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to open event store");
        return -1;
    }
    return 0;
}

static void pystore_dealloc(PyEventStore* self) {
    if (self->store) {
        event_store_close(self->store);
        self->store = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

// ── Helper: map FE error codes to Python exceptions ──────────────────────────

static PyObject* fe_error(int err) {
    switch (err) {
        case FE_INVALID_ARG:      PyErr_SetString(PyExc_ValueError,    "Invalid argument"); break;
        case FE_OUT_OF_MEMORY:    PyErr_SetString(PyExc_MemoryError,   "Out of memory");    break;
        case FE_CAPACITY_EXCEEDED:PyErr_SetString(PyExc_OverflowError, "Capacity exceeded");break;
        case FE_NOT_FOUND:        PyErr_SetString(PyExc_KeyError,      "Not found");        break;
        case FE_IO_ERROR:         PyErr_SetString(PyExc_IOError,       "I/O error");        break;
        case FE_CORRUPT_DATA:     PyErr_SetString(PyExc_ValueError,    "Corrupt data");     break;
        default:                  PyErr_SetString(PyExc_RuntimeError,  "Unknown error");    break;
    }
    return NULL;
}

// ── Methods ───────────────────────────────────────────────────────────────────

static PyObject* pystore_append(PyEventStore* self, PyObject* args, PyObject* kwargs) {
    static char* kwlist[] = {"event_type", "user_id", "timestamp", "properties", NULL};
    const char* event_type;
    unsigned long long user_id;
    unsigned long long timestamp;
    const char* properties = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sKK|z", kwlist,
                                     &event_type, &user_id, &timestamp, &properties))
        return NULL;

    int err = event_store_append(self->store, event_type,
                                 (uint64_t)user_id, (uint64_t)timestamp, properties);
    if (err != FE_OK) return fe_error(err);

    Py_RETURN_NONE;
}

static PyObject* pystore_flush(PyEventStore* self, PyObject* args) {
    int err = event_store_flush(self->store);
    if (err != FE_OK) return fe_error(err);
    Py_RETURN_NONE;
}

static PyObject* pystore_size(PyEventStore* self, PyObject* args) {
    return PyLong_FromSize_t(event_store_size(self->store));
}

static PyObject* pystore_close(PyEventStore* self, PyObject* args) {
    if (self->store) {
        event_store_close(self->store);
        self->store = NULL;
    }
    Py_RETURN_NONE;
}

// Convert a QueryResult row to a Python dict.
static PyObject* row_to_dict(QueryResult* r, size_t i,
                              const StringDict* dict) {
    PyObject* d = PyDict_New();
    if (!d) return NULL;

    // Resolve type_id back to string if possible
    const char* type_str = NULL;
    if (dict) {
        for (size_t j = 0; j < dict->capacity; j++) {
            if (dict->strings[j] && dict->ids[j] == r->event_type_ids[i]) {
                type_str = dict->strings[j];
                break;
            }
        }
    }

    PyObject* val;

    val = type_str ? PyUnicode_FromString(type_str)
                   : PyLong_FromUnsignedLong(r->event_type_ids[i]);
    if (!val || PyDict_SetItemString(d, "event_type", val) < 0) goto err;
    Py_DECREF(val);

    val = PyLong_FromUnsignedLongLong(r->user_ids[i]);
    if (!val || PyDict_SetItemString(d, "user_id", val) < 0) goto err;
    Py_DECREF(val);

    val = PyLong_FromUnsignedLongLong(r->timestamps[i]);
    if (!val || PyDict_SetItemString(d, "timestamp", val) < 0) goto err;
    Py_DECREF(val);

    if (r->properties[i]) {
        val = PyUnicode_FromString(r->properties[i]);
    } else {
        val = Py_None;
        Py_INCREF(Py_None);
    }
    if (!val || PyDict_SetItemString(d, "properties", val) < 0) goto err;
    Py_DECREF(val);

    return d;
err:
    Py_DECREF(d);
    return NULL;
}

static PyObject* pystore_filter(PyEventStore* self, PyObject* args, PyObject* kwargs) {
    static char* kwlist[] = {"event_type", "user_id", "start_ts", "end_ts", NULL};
    const char* event_type = NULL;
    unsigned long long user_id  = 0;
    unsigned long long start_ts = 0;
    unsigned long long end_ts   = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|zKKK", kwlist,
                                     &event_type, &user_id, &start_ts, &end_ts))
        return NULL;

    QueryResult* result = event_store_filter(self->store, event_type,
                                             (uint64_t)user_id,
                                             (uint64_t)start_ts,
                                             (uint64_t)end_ts);
    if (!result) {
        PyErr_SetString(PyExc_RuntimeError, "Filter failed");
        return NULL;
    }

    PyObject* list = PyList_New((Py_ssize_t)result->count);
    if (!list) { query_result_destroy(result); return NULL; }

    for (size_t i = 0; i < result->count; i++) {
        PyObject* row = row_to_dict(result, i, self->store->event_dict);
        if (!row) {
            Py_DECREF(list);
            query_result_destroy(result);
            return NULL;
        }
        PyList_SET_ITEM(list, (Py_ssize_t)i, row);  // steals reference
    }

    query_result_destroy(result);
    return list;
}

// Scan segment metadata + active block for the global min timestamp.
// Returns None if the store is empty. No disk I/O.
static PyObject* pystore_min_timestamp(PyEventStore* self, PyObject* args) {
    EventStore* s = self->store;
    uint64_t min_ts = UINT64_MAX;

    for (size_t i = 0; i < s->num_segments; i++)
        if (s->segments[i]->min_timestamp < min_ts)
            min_ts = s->segments[i]->min_timestamp;

    if (s->active_block && s->active_block->count > 0)
        if (s->active_block->min_timestamp < min_ts)
            min_ts = s->active_block->min_timestamp;

    if (min_ts == UINT64_MAX) Py_RETURN_NONE;
    return PyLong_FromUnsignedLongLong(min_ts);
}

// Scan segment metadata + active block for the global max timestamp.
// Returns None if the store is empty. No disk I/O.
static PyObject* pystore_max_timestamp(PyEventStore* self, PyObject* args) {
    EventStore* s = self->store;
    uint64_t max_ts = 0;
    int found = 0;

    for (size_t i = 0; i < s->num_segments; i++) {
        if (s->segments[i]->max_timestamp > max_ts) {
            max_ts = s->segments[i]->max_timestamp;
            found = 1;
        }
    }

    if (s->active_block && s->active_block->count > 0) {
        if (s->active_block->max_timestamp > max_ts) {
            max_ts = s->active_block->max_timestamp;
            found = 1;
        }
    }

    if (!found) Py_RETURN_NONE;
    return PyLong_FromUnsignedLongLong(max_ts);
}

// ── Context manager support (with EventStore(...) as db) ──────────────────────

static PyObject* pystore_enter(PyEventStore* self, PyObject* args) {
    Py_INCREF(self);
    return (PyObject*)self;
}

static PyObject* pystore_exit(PyEventStore* self, PyObject* args) {
    if (self->store) {
        event_store_close(self->store);
        self->store = NULL;
    }
    Py_RETURN_FALSE;  // don't suppress exceptions
}

// ── Method table ──────────────────────────────────────────────────────────────

static PyMethodDef pystore_methods[] = {
    {"append", (PyCFunction)pystore_append, METH_VARARGS | METH_KEYWORDS,
     "append(event_type, user_id, timestamp, properties=None)\n"
     "Append an event to the store."},
    {"filter", (PyCFunction)pystore_filter, METH_VARARGS | METH_KEYWORDS,
     "filter(event_type=None, user_id=0, start_ts=0, end_ts=0) -> list[dict]\n"
     "Filter events. Returns a list of dicts with keys: event_type, user_id, timestamp, properties."},
    {"flush",  (PyCFunction)pystore_flush,  METH_NOARGS,
     "flush()\nFlush the active block to a segment file."},
    {"min_timestamp", (PyCFunction)pystore_min_timestamp, METH_NOARGS,
     "min_timestamp() -> int | None\nEarliest timestamp in the store. No disk I/O."},
    {"max_timestamp", (PyCFunction)pystore_max_timestamp, METH_NOARGS,
     "max_timestamp() -> int | None\nLatest timestamp in the store. No disk I/O."},
    {"size",   (PyCFunction)pystore_size,   METH_NOARGS,
     "size() -> int\nTotal number of events in the store."},
    {"close",  (PyCFunction)pystore_close,  METH_NOARGS,
     "close()\nClose the store and flush pending writes."},
    {"__enter__", (PyCFunction)pystore_enter, METH_NOARGS, NULL},
    {"__exit__",  (PyCFunction)pystore_exit,  METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

// ── Type definition ───────────────────────────────────────────────────────────

static PyTypeObject PyEventStoreType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name      = "_fastevents.EventStore",
    .tp_basicsize = sizeof(PyEventStore),
    .tp_new       = pystore_new,
    .tp_init      = (initproc)pystore_init,
    .tp_dealloc   = (destructor)pystore_dealloc,
    .tp_methods   = pystore_methods,
    .tp_flags     = Py_TPFLAGS_DEFAULT,
    .tp_doc       = "EventStore(path)\n\nOpen or create an event store at the given directory path.",
};

// ============================================================================
// Module definition
// ============================================================================

static PyModuleDef fastevents_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_fastevents",
    .m_doc  = "FastEvents — embedded columnar event store (internal C extension).",
    .m_size = -1,
};

PyMODINIT_FUNC PyInit__fastevents(void) {
    if (PyType_Ready(&PyEventStoreType) < 0) return NULL;

    PyObject* m = PyModule_Create(&fastevents_module);
    if (!m) return NULL;

    Py_INCREF(&PyEventStoreType);
    if (PyModule_AddObject(m, "EventStore", (PyObject*)&PyEventStoreType) < 0) {
        Py_DECREF(&PyEventStoreType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}

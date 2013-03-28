/*
 * Copyright 2013 10gen Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "cbson-util.h"
#include "time64.h"

#include <datetime.h>


typedef struct
{
   _PyTZINFO_HEAD
   PyObject *name;
   PyObject *offset;
} cbson_fixed_offset_t;


static PyTypeObject cbson_fixed_offset_type = {
   PyObject_HEAD_INIT(NULL)
   0,                         /*ob_size*/
   "cbson.FixedOffset",       /*tp_name*/
   sizeof(cbson_fixed_offset_t), /*tp_basicsize*/
   0,                         /*tp_itemsize*/
   0,                         /*tp_dealloc*/
   0,                         /*tp_print*/
   0,                         /*tp_getattr*/
   0,                         /*tp_setattr*/
   0,                         /*tp_compare*/
   0,                         /*tp_repr*/
   0,                         /*tp_as_number*/
   0,                         /*tp_as_sequence*/
   0,                         /*tp_as_mapping*/
   0,                         /*tp_hash */
   0,                         /*tp_call*/
   0,                         /*tp_str*/
   0,                         /*tp_getattro*/
   0,                         /*tp_setattro*/
   0,                         /*tp_as_buffer*/
   Py_TPFLAGS_DEFAULT,        /*tp_flags*/
   "A fixed offset tzinfo.",  /*tp_doc*/
};


static PyObject *
cbson_fixed_offset_tp_new (PyTypeObject *self,
                           PyObject     *args,
                           PyObject     *kwargs)
{
   PyObject *offset;
   PyObject *name;
   PyObject *ret;

   if (!PyArg_ParseTuple(args, "OO", &offset, &name)) {
      return NULL;
   }

   if (PyDelta_Check(offset)) {
      Py_INCREF(offset);
   } else if (PyInt_Check(offset)) {
      PyObject *dkwargs;

      dkwargs = PyDict_New();
      PyDict_SetItemString(dkwargs, "minutes", offset);
      offset = PyType_GenericNew(PyDateTimeAPI->DeltaType, NULL, dkwargs);
      Py_DECREF(dkwargs);
   } else {
      PyErr_SetString(PyExc_TypeError, "`offset` must be timedelta or int.");
      return NULL;
   }

   if (PyString_Check(name) || PyUnicode_Check(name)) {
      Py_INCREF(name);
   } else {
      PyErr_SetString(PyExc_TypeError, "`name` must be a str or unicode.");
      Py_DECREF(offset);
      return NULL;
   }

   ret = PyType_GenericNew(self, args, kwargs);
   ((cbson_fixed_offset_t *)ret)->name = name;
   ((cbson_fixed_offset_t *)ret)->offset = offset;

   return ret;
}


static void
cbson_fixed_offset_tp_dealloc (PyObject *self)
{
   cbson_fixed_offset_t *offset = (cbson_fixed_offset_t *)self;
   Py_XDECREF(offset->offset);
   Py_XDECREF(offset->name);
   self->ob_type->tp_free(self);
}


static PyObject *
cbson_fixed_offset_dst (PyObject *self,
                        PyObject *args)
{
   return PyDelta_FromDSU(0, 0, 0);
}


static PyObject *
cbson_fixed_offset_tzname (PyObject *self,
                           PyObject *args)
{
   cbson_fixed_offset_t *offset = (cbson_fixed_offset_t *)self;
   Py_INCREF(offset->name);
   return offset->name;
}


static PyObject *
cbson_fixed_offset_utcoffset (PyObject *self,
                              PyObject *args)
{
   cbson_fixed_offset_t *offset = (cbson_fixed_offset_t *)self;
   Py_INCREF(offset->offset);
   return offset->offset;
}


static PyMethodDef cbson_fixed_offset_tp_methods[] = {
   { (char *)"dst", cbson_fixed_offset_dst, METH_VARARGS },
   { (char *)"utcoffset", cbson_fixed_offset_utcoffset, METH_VARARGS },
   { (char *)"tzname", cbson_fixed_offset_tzname, METH_VARARGS },
   { NULL }
};


PyTypeObject *
cbson_fixed_offset_get_type (void)
{
   static bson_bool_t initialized;

   if (!initialized) {
#define SET(n, v) cbson_fixed_offset_type.n = v

      SET(tp_new, cbson_fixed_offset_tp_new);
      SET(tp_base, PyDateTimeAPI->TZInfoType);
      SET(tp_dealloc, cbson_fixed_offset_tp_dealloc);
      SET(tp_methods, cbson_fixed_offset_tp_methods);

      if (PyType_Ready(&cbson_fixed_offset_type) < 0) {
         assert(FALSE);
         return NULL;
      }

      initialized = TRUE;

#undef SET
   }

   return &cbson_fixed_offset_type;
}


bson_bool_t
cbson_util_init (PyObject *module)
{
   PyDateTime_IMPORT;

   if (!PyDateTimeAPI) {
      return FALSE;
   }

   PyModule_AddObject(module, "FixedOffset",
                      (PyObject *)cbson_fixed_offset_get_type());

   return TRUE;
}


bson_bool_t
cbson_date_time_check (PyObject *object)
{
   return PyDateTime_Check(object);
}


PyObject *
cbson_date_time_from_msec (bson_int64_t  msec_since_epoch,
                           PyObject     *tzinfo)
{
   bson_int32_t diff;
   bson_int32_t microseconds;
   struct TM timeinfo;
   Time64_T seconds;
   PyObject *ret;

   /* To encode a datetime instance like datetime(9999, 12, 31, 23, 59, 59, 999999)
    * we follow these steps:
    * 1. Calculate a timestamp in seconds:       253402300799
    * 2. Multiply that by 1000:                  253402300799000
    * 3. Add in microseconds divided by 1000     253402300799999
    *
    * (Note: BSON doesn't support microsecond accuracy, hence the rounding.)
    *
    * To decode we could do:
    * 1. Get seconds: timestamp / 1000:          253402300799
    * 2. Get micros: (timestamp % 1000) * 1000:  999000
    * Resulting in datetime(9999, 12, 31, 23, 59, 59, 999000) -- the expected result
    *
    * Now what if the we encode (1, 1, 1, 1, 1, 1, 111111)?
    * 1. and 2. gives:                           -62135593139000
    * 3. Gives us:                               -62135593138889
    *
    * Now decode:
    * 1. Gives us:                               -62135593138
    * 2. Gives us:                               -889000
    * Resulting in datetime(1, 1, 1, 1, 1, 2, 15888216) -- an invalid result
    *
    * If instead to decode we do:
    * diff = ((millis % 1000) + 1000) % 1000:    111
    * seconds = (millis - diff) / 1000:          -62135593139
    * micros = diff * 1000                       111000
    * Resulting in datetime(1, 1, 1, 1, 1, 1, 111000) -- the expected result
    */

   diff = ((msec_since_epoch % 1000L) + 1000) % 1000;
   microseconds = diff * 1000;
   seconds = (msec_since_epoch - diff) / 1000;
   gmtime64_r(&seconds, &timeinfo);

   ret = PyDateTime_FromDateAndTime(timeinfo.tm_year + 1900,
                                    timeinfo.tm_mon + 1,
                                    timeinfo.tm_mday,
                                    timeinfo.tm_hour,
                                    timeinfo.tm_min,
                                    timeinfo.tm_sec,
                                    microseconds);

   if (ret && tzinfo) {
      PyObject *tmp = ret;
      PyObject *method;
      PyObject *mkwargs;
      PyObject *margs;

      method = PyObject_GetAttrString(ret, "replace");
      mkwargs = PyDict_New();
      margs = PyTuple_New(0);
      PyDict_SetItemString(mkwargs, "tzinfo", tzinfo);
      ret = PyObject_Call(method, margs, mkwargs);

      Py_DECREF(method);
      Py_DECREF(mkwargs);
      Py_DECREF(margs);
      Py_DECREF(tmp);
   }

   return ret;
}


bson_int32_t
cbson_date_time_seconds (PyObject *date_time)
{
   bson_int32_t val = -1;
   PyObject *calendar;
   PyObject *ret;
   PyObject *timegm;
   PyObject *timetuple;
   PyObject *offset;

   /*
    * TODO: We can get about a 30% performance improvement for tight loops if
    *       the calendar module is cached. However, that complicates other
    *       issues like handling unexpected fork() by various modules.
    */

   bson_return_val_if_fail(PyDateTime_Check(date_time), 0);

   if (!(calendar = PyImport_ImportModule("calendar"))) {
      return 0;
   }

   if (!(timegm = PyObject_GetAttrString(calendar, "timegm"))) {
      goto dec_calendar;
   }

   if (!(offset = PyObject_CallMethod(date_time, (char *)"utcoffset", NULL))) {
      goto dec_timegm;
   }

   if (offset != Py_None) {
      date_time = PyNumber_Subtract(date_time, offset);
   } else {
      Py_INCREF(date_time);
   }

   if (!(timetuple = PyObject_CallMethod(date_time, (char *)"timetuple", NULL))) {
      goto dec_offset;
   }

   if (!(ret = PyObject_CallFunction(timegm, (char *)"O", timetuple))) {
      goto dec_timetuple;
   }

   val = PyInt_AS_LONG(ret);

   Py_DECREF(ret);

dec_timetuple:
   Py_DECREF(timetuple);
dec_offset:
   Py_DECREF(offset);
   Py_DECREF(date_time);
dec_timegm:
   Py_DECREF(timegm);
dec_calendar:
   Py_DECREF(calendar);

   return val;
}


PyObject *
cbson_fixed_offset_utc_ref (void)
{
   static PyObject *utc;

   if (!utc) {
      PyObject *args;

      args = Py_BuildValue("is#", 0, "UTC", 3);
      utc = cbson_fixed_offset_tp_new(cbson_fixed_offset_get_type(), args, NULL);
      Py_DECREF(args);
   }

   Py_INCREF(utc);

   return utc;
}

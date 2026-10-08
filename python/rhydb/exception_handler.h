#pragma once
#include <Python.h>
#include <stdexcept>
#include "rhydb/query_engine/illegal_query_exception.h"

inline void handleRhyDBException() {
   try {
      throw;  // re-throw current exception
   } catch (const rhydb::query_engine::IllegalQueryException& e) {
      PyErr_SetString(PyExc_ValueError, e.what());
   } catch (const std::runtime_error& e) {
      PyErr_SetString(PyExc_RuntimeError, e.what());
   } catch (const std::exception& e) {
      PyErr_SetString(PyExc_RuntimeError, e.what());
   } catch (...) {
      PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
   }
}

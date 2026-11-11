#pragma once
#include <Python.h>
#include <stdexcept>
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/query_execution_error.h"

inline void handleRhyDBException() {
   try {
      throw;  // re-throw current exception
   } catch (const rhydb::query_engine::IllegalQueryException& e) {
      PyErr_SetString(PyExc_ValueError, e.what());
   } catch (const rhydb::query_engine::QueryExecutionException& e) {
      // A failure caused by the user's query or data (e.g. a cast that cannot convert a column
      // value). Classify it as a ValueError like other user/query errors, not a RuntimeError.
      PyErr_SetString(PyExc_ValueError, e.what());
   } catch (const std::runtime_error& e) {
      PyErr_SetString(PyExc_RuntimeError, e.what());
   } catch (const std::exception& e) {
      PyErr_SetString(PyExc_RuntimeError, e.what());
   } catch (...) {
      PyErr_SetString(PyExc_RuntimeError, "Unknown C++ exception");
   }
}

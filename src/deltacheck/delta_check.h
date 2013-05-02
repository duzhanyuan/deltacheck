/*******************************************************************\

Module: Delta Checking

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_DELTA_CHECK_H
#define CPROVER_DELTA_CHECK_H

#include <string>

#include <util/message.h>

void delta_check(
  const std::string &index1,
  const std::string &index2,
  const std::string &function,
  message_handlert &);

#endif

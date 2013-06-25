/*******************************************************************\

Module: Definition Analysis

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_SOURCE_DOMAIN_H
#define CPROVER_SOURCE_DOMAIN_H

#include <analyses/static_analysis.h>
#include <analyses/interval_analysis.h>

class def_domaint:public domain_baset
{
public:
  // identifier to definition (source) location
  typedef std::map<irep_idt, std::set<locationt> > def_mapt;
  def_mapt def_map;

  virtual void transform(
    const namespacet &ns,
    locationt from,
    locationt to);
              
  virtual void output(
    const namespacet &ns,
    std::ostream &out) const;

  bool merge(const def_domaint &b);

protected:
  void assign(const exprt &lhs, locationt from);
};

#endif
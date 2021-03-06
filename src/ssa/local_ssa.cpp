/*******************************************************************\

Module: SSA

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

//#define DEBUG

#ifdef DEBUG
#include <iostream>
#include <langapi/language_util.h>
#endif

#include <util/i2string.h>
#include <util/expr_util.h>
#include <util/decision_procedure.h>
#include <util/byte_operators.h>

#include <goto-symex/adjust_float_expressions.h>

#include <langapi/language_util.h>

#include "local_ssa.h"
#include "malloc_ssa.h"
#include "ssa_dereference.h"
#include "address_canonizer.h"

/*******************************************************************\

Function: local_SSAt::build_SSA

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::build_SSA()
{
  // perform SSA data-flow analysis
  ssa_analysis(goto_function, ns);
  
  // now build phi-nodes
  forall_goto_program_instructions(i_it, goto_function.body)
    build_phi_nodes(i_it);
  
  // now build transfer functions
  forall_goto_program_instructions(i_it, goto_function.body)
    build_transfer(i_it);

  // now build branching conditions
  forall_goto_program_instructions(i_it, goto_function.body)
    build_cond(i_it);

  // now build guards
  forall_goto_program_instructions(i_it, goto_function.body)
    build_guard(i_it);

  // now build assertions
  forall_goto_program_instructions(i_it, goto_function.body)
    build_assertions(i_it);
}

/*******************************************************************\

Function: local_SSAt::edge_guard

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::edge_guard(locationt from, locationt to) const
{
  if(from->is_goto())
  {
    // big question: taken or not taken?
    if(to==from->get_target())
      return and_exprt(guard_symbol(from), cond_symbol(from));
    else
      return and_exprt(guard_symbol(from), not_exprt(cond_symbol(from)));
  }
  else if(from->is_assume())
  {
    return and_exprt(guard_symbol(from), cond_symbol(from));
  }
  else
    return guard_symbol(from);
}

/*******************************************************************\

Function: local_SSAt::build_phi_nodes

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::build_phi_nodes(locationt loc)
{
  const ssa_domaint::phi_nodest &phi_nodes=ssa_analysis[loc].phi_nodes;
  nodet &node=nodes[loc];

  for(objectst::const_iterator
      o_it=ssa_objects.objects.begin();
      o_it!=ssa_objects.objects.end(); o_it++)
  {
    // phi-node for this object here?
    ssa_domaint::phi_nodest::const_iterator p_it=
      phi_nodes.find(o_it->get_identifier());
          
    if(p_it==phi_nodes.end()) continue; // none
    
    // Yes. Get the source -> def map.
    const std::map<locationt, ssa_domaint::deft> &incoming=p_it->second;

    exprt rhs=nil_exprt();

    // We distinguish forwards- from backwards-edges,
    // and do forwards-edges first, which gives them
    // _lower_ priority in the ITE. Inputs are always
    // forward edges.
    
    for(std::map<locationt, ssa_domaint::deft>::const_iterator
        incoming_it=incoming.begin();
        incoming_it!=incoming.end();
        incoming_it++)
      if(incoming_it->second.is_input() ||
         incoming_it->first->location_number < loc->location_number)
      {
        // it's a forward edge
        exprt incoming_value=name(*o_it, incoming_it->second);
        exprt incoming_guard=edge_guard(incoming_it->first, loc);

        if(rhs.is_nil()) // first
          rhs=incoming_value;
        else
          rhs=if_exprt(incoming_guard, incoming_value, rhs);
      }
     
    // now do backwards

    for(std::map<locationt, ssa_domaint::deft>::const_iterator
        incoming_it=incoming.begin();
        incoming_it!=incoming.end();
        incoming_it++)
      if(!incoming_it->second.is_input() &&
         incoming_it->first->location_number >= loc->location_number)
      {
        // it's a backwards edge
        exprt incoming_value=name(*o_it, LOOP_BACK, incoming_it->first);
        exprt incoming_select=name(guard_symbol(), LOOP_SELECT, incoming_it->first);

        if(rhs.is_nil()) // first
          rhs=incoming_value;
        else
          rhs=if_exprt(incoming_select, incoming_value, rhs);
      }

    symbol_exprt lhs=name(*o_it, PHI, loc);
    
    equal_exprt equality(lhs, rhs);
    node.equalities.push_back(equality);
  }
}

/*******************************************************************\

Function: local_SSAt::dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::dereference(const exprt &src, locationt loc) const
{
  const ssa_value_domaint &ssa_value_domain=ssa_value_ai[loc];
  const std::string nondet_prefix="deref#"+i2string(loc->location_number);
  return ::dereference(src, ssa_value_domain, nondet_prefix, ns);
}

/*******************************************************************\

Function: local_SSAt::build_transfer

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::build_transfer(locationt loc)
{
  if(loc->is_assign())
  {
    const code_assignt &code_assign=to_code_assign(loc->code);

    exprt deref_lhs=dereference(code_assign.lhs(), loc);
    exprt deref_rhs=dereference(code_assign.rhs(), loc);

    assign_rec(deref_lhs, deref_rhs, true_exprt(), loc);
  }
  else if(loc->is_function_call())
  {
    const code_function_callt &code_function_call=
      to_code_function_call(loc->code);
      
    const exprt &lhs=code_function_call.lhs();
      
    if(lhs.is_not_nil())
    {
      exprt deref_lhs=dereference(lhs, loc);
    
      // generate a symbol for rhs
      irep_idt identifier="ssa::return_value"+i2string(loc->location_number);
      symbol_exprt rhs(identifier, code_function_call.lhs().type());
      
      assign_rec(deref_lhs, rhs, true_exprt(), loc);
    }
  }
}
  
/*******************************************************************\

Function: local_SSAt::build_cond

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::build_cond(locationt loc)
{
  // anything to be built?
  if(!loc->is_goto() &&
     !loc->is_assume()) return;
  
  // produce a symbol for the renamed branching condition
  equal_exprt equality(cond_symbol(loc), read_rhs(loc->guard, loc));
  nodes[loc].equalities.push_back(equality);
}

/*******************************************************************\

Function: local_SSAt::build_guard

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::build_guard(locationt loc)
{
  const guard_mapt::entryt &entry=guard_map[loc];
  
  // anything to be built?
  if(!entry.has_guard) return;
  
  exprt::operandst sources;

  // the very first 'loc' trivially gets 'true' as source
  if(loc==goto_function.body.instructions.begin())
    sources.push_back(true_exprt());
    
  for(guard_mapt::incomingt::const_iterator
      i_it=entry.incoming.begin();
      i_it!=entry.incoming.end();
      i_it++)
  {
    const guard_mapt::edget &edge=*i_it;
    
    exprt source;
    
    // might be backwards branch taken edge
    if(edge.is_branch_taken() &&
       edge.from->is_backwards_goto())
    {
      // The loop selector indicates whether the path comes from
      // above (entering the loop) or below (iterating).
      // By convention, we use the loop select symbol for the location
      // of the backwards goto.
      symbol_exprt loop_select=
        name(guard_symbol(), LOOP_SELECT, edge.from);

      #if 0
      source=false_exprt();
      #else
      // need constraing for edge.cond
      source=loop_select;
      #endif
    }
    else
    {
      // the other cases are basically similar
      
      symbol_exprt gs=name(guard_symbol(), OUT, edge.guard_source);
      exprt cond;
      
      if(edge.is_branch_taken() ||
         edge.is_assume() ||
         edge.is_function_call())
        cond=cond_symbol(edge.from);
      else if(edge.is_branch_not_taken())
        cond=boolean_negate(cond_symbol(edge.from));
      else if(edge.is_successor())
        cond=true_exprt();
      else
        assert(false);

      source=and_exprt(gs, cond);
    }
    
    sources.push_back(source);
  }

  // the below produces 'false' if there is no source
  exprt rhs=disjunction(sources);
  
  equal_exprt equality(guard_symbol(loc), rhs);
  nodes[loc].equalities.push_back(equality);
}

/*******************************************************************\

Function: local_SSAt::build_assertions

  Inputs:

 Outputs:

 Purpose: turns assertions into constraints

\*******************************************************************/

void local_SSAt::build_assertions(locationt loc)
{
  if(loc->is_assert())
  {
    exprt c=read_rhs(loc->guard, loc);
    exprt g=guard_symbol(loc);    
    nodes[loc].assertion=implies_exprt(g, c);
  }
}

/*******************************************************************\

Function: local_SSAt::assertion

  Inputs:

 Outputs:

 Purpose: returns assertion for a given location

\*******************************************************************/

exprt local_SSAt::assertion(locationt loc) const
{
  nodest::const_iterator n_it=nodes.find(loc);
  if(n_it==nodes.end()) return nil_exprt();
  return n_it->second.assertion;
}

/*******************************************************************\

Function: local_SSAt::assertions_to_constraints

  Inputs:

 Outputs:

 Purpose: turns assertions into constraints

\*******************************************************************/

void local_SSAt::assertions_to_constraints()
{
  for(nodest::iterator
      n_it=nodes.begin();
      n_it!=nodes.end();
      n_it++)
  {
    nodet &node=n_it->second;
    if(node.assertion.is_not_nil())
      node.constraints.push_back(node.assertion);
  }  
}

/*******************************************************************\

Function: local_SSAt::cond_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

ssa_objectt local_SSAt::cond_symbol() const
{
  return ssa_objectt(symbol_exprt("ssa::$cond", bool_typet()), ns);
}

/*******************************************************************\

Function: local_SSAt::guard_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

ssa_objectt local_SSAt::guard_symbol() const
{
  return ssa_objectt(symbol_exprt("ssa::$guard", bool_typet()), ns);
}

/*******************************************************************\

Function: local_SSAt::read_rhs

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt local_SSAt::read_rhs(
  const ssa_objectt &object,
  locationt loc) const
{
  const irep_idt &identifier=object.get_identifier();
  const ssa_domaint &ssa_domain=ssa_analysis[loc];

  ssa_domaint::def_mapt::const_iterator d_it=
    ssa_domain.def_map.find(identifier);

  if(d_it==ssa_domain.def_map.end())
  {
    // not written so far, it's input
    return name_input(object);
  }
  else
    return name(object, d_it->second.def);
}

/*******************************************************************\

Function: local_SSAt::read_lhs

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::read_lhs(
  const exprt &expr,
  locationt loc) const
{
  // dereference first
  exprt tmp1=dereference(expr, loc);

  ssa_objectt object(tmp1, ns);

  // is this an object we track?
  if(ssa_objects.objects.find(object)!=
     ssa_objects.objects.end())
  {
    // yes, it is
    if(assignments.assigns(loc, object))
      return name(object, OUT, loc);
    else
      return read_rhs(object, loc);
  }
  else
    return read_rhs(tmp1, loc);
}

/*******************************************************************\

Function: local_SSAt::read_node_in

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::read_node_in(
  const ssa_objectt &object,
  locationt loc) const
{
  // This reads:
  // * LOOP_BACK if there is a LOOP node at 'loc' for symbol
  // * OUT otherwise

  const irep_idt &identifier=object.get_identifier();
  const ssa_domaint &ssa_domain=ssa_analysis[loc];

  ssa_domaint::def_mapt::const_iterator d_it=
    ssa_domain.def_map.find(identifier);

  if(d_it==ssa_domain.def_map.end())
    return name_input(object); // not written so far

  const ssa_domaint::phi_nodest &phi_nodes=ssa_analysis[loc].phi_nodes;

  ssa_domaint::phi_nodest::const_iterator p_it=
    phi_nodes.find(identifier);
    
  bool has_phi=false;
        
  if(p_it!=phi_nodes.end())
  {
    const std::map<locationt, ssa_domaint::deft> &incoming=p_it->second;

    for(std::map<locationt, ssa_domaint::deft>::const_iterator
        incoming_it=incoming.begin();
        incoming_it!=incoming.end();
        incoming_it++)
    {
      if(incoming_it->first->location_number > loc->location_number)
        has_phi=true;
    }
  }
  
  if(has_phi)
    return name(object, LOOP_BACK, loc);
  else
    return read_rhs(object, loc);
}

/*******************************************************************\

Function: local_SSAt::read_rhs

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::read_rhs(const exprt &expr, locationt loc) const
{
  exprt tmp1=expr;
  adjust_float_expressions(tmp1, ns);
  
  unsigned counter=0;
  replace_side_effects_rec(tmp1, loc, counter);

  #ifdef DEBUG
  std::cout << "read_rhs tmp1: " << from_expr(ns, "", tmp1) << '\n';
  #endif
  
  exprt tmp2=dereference(tmp1, loc);

  #ifdef DEBUG
  std::cout << "read_rhs tmp2: " << from_expr(ns, "", tmp2) << '\n';
  #endif
  
  exprt result=read_rhs_rec(tmp2, loc);
  
  #ifdef DEBUG
  std::cout << "read_rhs result: " << from_expr(ns, "", result) << '\n';
  #endif
  
  return result;
}

/*******************************************************************\

Function: local_SSAt::read_rhs_address_of_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::read_rhs_address_of_rec(
  const exprt &expr,
  locationt loc) const
{
  if(expr.id()==ID_dereference)
  {
    // We 'read' the pointer only, the dereferencing expression stays.
    dereference_exprt tmp=to_dereference_expr(expr);
    tmp.pointer()=read_rhs_rec(tmp.pointer(), loc);
    return tmp;
  }
  else if(expr.id()==ID_member)
  {
    member_exprt tmp=to_member_expr(expr);
    tmp.struct_op()=read_rhs_address_of_rec(tmp.struct_op(), loc);
    return tmp;
  }
  else if(expr.id()==ID_index)
  {
    index_exprt tmp=to_index_expr(expr);
    tmp.array()=read_rhs_address_of_rec(tmp.array(), loc);
    tmp.index()=read_rhs_rec(tmp.index(), loc);
    return tmp;
  }
  else if(expr.id()==ID_if)
  {
    if_exprt tmp=to_if_expr(expr);
    tmp.cond()=read_rhs_rec(tmp.cond(), loc);
    tmp.true_case()=read_rhs_address_of_rec(tmp.true_case(), loc);
    tmp.false_case()=read_rhs_address_of_rec(tmp.false_case(), loc);
    return tmp;
  }
  else
    return expr;
}

/*******************************************************************\

Function: local_SSAt::read_rhs_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt local_SSAt::read_rhs_rec(const exprt &expr, locationt loc) const
{
  if(expr.id()==ID_side_effect)
  {
    throw "unexpected side effect in read_rhs_rec";
  }
  else if(expr.id()==ID_address_of)
  {
    address_of_exprt tmp=to_address_of_expr(expr);
    tmp.object()=read_rhs_address_of_rec(tmp.object(), loc);
    return address_canonizer(tmp, ns);
  }
  else if(expr.id()==ID_dereference)
  {
    throw "unexpected dereference in read_rhs_rec";
  }
  else if(expr.id()==ID_index)
  {
    const index_exprt &index_expr=to_index_expr(expr);
    return index_exprt(read_rhs(index_expr.array(), loc),
                       read_rhs(index_expr.index(), loc),
                       expr.type());
  }
  
  ssa_objectt object(expr, ns);
  
  // is it an object identifier?
  
  if(!object)
  {
    exprt tmp=expr; // copy
    Forall_operands(it, tmp)
      *it=read_rhs(*it, loc);
    return tmp;
  }
  
  // Argument is a struct-typed ssa object?
  // May need to split up into members.
  const typet &type=ns.follow(expr.type());

  if(type.id()==ID_struct)
  {
    // build struct constructor
    struct_exprt result(expr.type());
  
    const struct_typet &struct_type=to_struct_type(type);
    const struct_typet::componentst &components=struct_type.components();
  
    result.operands().resize(components.size());
  
    for(struct_typet::componentst::const_iterator
        it=components.begin();
        it!=components.end();
        it++)
    {
      result.operands()[it-components.begin()]=
        read_rhs(member_exprt(expr, it->get_name(), it->type()), loc);
    }
  
    return result;
  }

  // is this an object we track?
  if(ssa_objects.objects.find(object)!=
     ssa_objects.objects.end())
  {
    return read_rhs(object, loc);
  }
  else
  {
    return name_input(object);
  }
}

/*******************************************************************\

Function: local_SSAt::replace_side_effects_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::replace_side_effects_rec(
  exprt &expr, locationt loc, unsigned &counter) const
{
  Forall_operands(it, expr)
    replace_side_effects_rec(*it, loc, counter);

  if(expr.id()==ID_side_effect)
  {
    const side_effect_exprt &side_effect_expr=
      to_side_effect_expr(expr);
    const irep_idt statement=side_effect_expr.get_statement();

    if(statement==ID_nondet)
    {
      // turn into nondet_symbol
      exprt nondet_symbol(ID_nondet_symbol, expr.type());
      counter++;
      const irep_idt identifier=
        "ssa::nondet"+
        i2string(loc->location_number)+
        "."+i2string(counter)+suffix;
      nondet_symbol.set(ID_identifier, identifier);
      
      expr.swap(nondet_symbol);
    }
    else if(statement==ID_malloc)
    {
      counter++;
      std::string tmp_suffix=
        i2string(loc->location_number)+
        "."+i2string(counter)+suffix;
      expr=malloc_ssa(side_effect_expr, tmp_suffix, ns);
    }
    else
      throw "unexpected side effect: "+id2string(statement);
  }
}

/*******************************************************************\

Function: local_SSAt::name

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt local_SSAt::name(
  const ssa_objectt &object,
  kindt kind,
  locationt loc) const
{
  symbol_exprt new_symbol_expr(object.get_expr().type());
  const irep_idt &id=object.get_identifier();
  unsigned cnt=loc->location_number;
  
  irep_idt new_id=id2string(id)+"#"+
                  (kind==PHI?"phi":
                   kind==LOOP_BACK?"lb":
                   kind==LOOP_SELECT?"ls":
                   "")+
                  i2string(cnt)+
                  (kind==LOOP_SELECT?std::string(""):suffix);

  new_symbol_expr.set_identifier(new_id);
  
  if(object.get_expr().source_location().is_not_nil())
    new_symbol_expr.add_source_location()=object.get_expr().source_location();
  
  return new_symbol_expr;
}

/*******************************************************************\

Function: local_SSAt::name

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt local_SSAt::name(
  const ssa_objectt &object,
  const ssa_domaint::deft &def) const
{
  if(def.is_input())
    return name_input(object);
  else if(def.is_phi())
    return name(object, PHI, def.loc);
  else
    return name(object, OUT, def.loc);
}

/*******************************************************************\

Function: local_SSAt::name_input

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt local_SSAt::name_input(const ssa_objectt &object) const
{
  symbol_exprt new_symbol_expr(object.get_expr().type()); // copy
  const irep_idt old_id=object.get_identifier();
  irep_idt new_id=id2string(old_id)+"#in"+suffix;
  new_symbol_expr.set_identifier(new_id);

  if(object.get_expr().source_location().is_not_nil())
    new_symbol_expr.add_source_location()=object.get_expr().source_location();

  return new_symbol_expr;
}

/*******************************************************************\

Function: local_SSAt::assign_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::assign_rec(
  const exprt &lhs,
  const exprt &rhs,
  const exprt &guard,
  locationt loc)
{
  const typet &type=ns.follow(lhs.type());

  if(is_symbol_struct_member(lhs, ns))
  {
    if(type.id()==ID_struct)
    {
      // need to split up

      const struct_typet &struct_type=to_struct_type(type);
      const struct_typet::componentst &components=struct_type.components();
      
      for(struct_typet::componentst::const_iterator
          it=components.begin();
          it!=components.end();
          it++)
      {
        member_exprt new_lhs(lhs, it->get_name(), it->type());
        member_exprt new_rhs(rhs, it->get_name(), it->type());
        assign_rec(new_lhs, new_rhs, guard, loc);
      }

      return;
    }

    ssa_objectt lhs_object(lhs, ns);
    
    const std::set<ssa_objectt> &assigned=
      assignments.get(loc);

    if(assigned.find(lhs_object)!=assigned.end())
    {
      exprt ssa_rhs=read_rhs(rhs, loc);

      const symbol_exprt ssa_symbol=name(lhs_object, OUT, loc);
      
      equal_exprt equality(ssa_symbol, ssa_rhs);
      nodes[loc].equalities.push_back(equality);
    }
  }
  else if(lhs.id()==ID_index)
  {
    const index_exprt &index_expr=to_index_expr(lhs);
    exprt ssa_array=index_expr.array();
    exprt new_rhs=with_exprt(ssa_array, index_expr.index(), rhs);
    assign_rec(index_expr.array(), new_rhs, guard, loc);
  }
  else if(lhs.id()==ID_member)
  {
    // These are non-flattened struct or union members.
    const member_exprt &member_expr=to_member_expr(lhs);
    const exprt &compound=member_expr.struct_op();
    const typet &compound_type=ns.follow(compound.type());

    if(compound_type.id()==ID_union)
    {
      union_exprt new_rhs(member_expr.get_component_name(), rhs, compound.type());
      assign_rec(member_expr.struct_op(), new_rhs, guard, loc);
    }
    else if(compound_type.id()==ID_struct)
    {
      exprt member_name(ID_member_name);
      member_name.set(ID_component_name, member_expr.get_component_name());
      with_exprt new_rhs(compound, member_name, rhs);
      assign_rec(compound, new_rhs, guard, loc);
    }
  }
  else if(lhs.id()==ID_complex_real)
  {
    assert(lhs.operands().size()==1);
    const exprt &op=lhs.op0();
    const complex_typet &complex_type=to_complex_type(op.type());
    exprt imag_op=unary_exprt(ID_complex_imag, op, complex_type.subtype());
    complex_exprt new_rhs(rhs, imag_op, complex_type);
    assign_rec(op, new_rhs, guard, loc);
  }
  else if(lhs.id()==ID_complex_imag)
  {
    assert(lhs.operands().size()==1);
    const exprt &op=lhs.op0();
    const complex_typet &complex_type=to_complex_type(op.type());
    exprt real_op=unary_exprt(ID_complex_real, op, complex_type.subtype());
    complex_exprt new_rhs(real_op, rhs, complex_type);
    assign_rec(op, new_rhs, guard, loc);
  }
  else if(lhs.id()==ID_if)
  {
    const if_exprt &if_expr=to_if_expr(lhs);
    assign_rec(if_expr.true_case(), rhs, and_exprt(guard, if_expr.cond()), loc);
    assign_rec(if_expr.false_case(), rhs, and_exprt(guard, not_exprt(if_expr.cond())), loc);
  }
  else if(lhs.id()==ID_byte_extract_little_endian ||
          lhs.id()==ID_byte_extract_big_endian)
  {
    const byte_extract_exprt &byte_extract_expr=to_byte_extract_expr(lhs);

    exprt new_lhs=byte_extract_expr.op();

    exprt new_rhs=byte_extract_exprt(
      byte_extract_expr.id(), rhs, byte_extract_expr.offset(), new_lhs.type());

    assign_rec(new_lhs, new_rhs, guard, loc);
  }
  else
    throw "UNKNOWN LHS: "+lhs.id_string();
}

/*******************************************************************\

Function: local_SSAt::output

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::output(std::ostream &out) const
{
  forall_goto_program_instructions(i_it, goto_function.body)
  {
    const nodest::const_iterator n_it=nodes.find(i_it);
    if(n_it==nodes.end()) continue;
    if(n_it->second.empty()) continue;
    n_it->second.output(out, ns);
    out << '\n';
  }
}

/*******************************************************************\

Function: local_SSAt::output_verbose

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::output_verbose(std::ostream &out) const
{
  forall_goto_program_instructions(i_it, goto_function.body)
  {
    out << "*** " << i_it->location_number
        << " " << i_it->source_location << "\n";

    const nodest::const_iterator n_it=nodes.find(i_it);

    if(n_it!=nodes.end())
      n_it->second.output(out, ns);
    
    out << "\n";
  }
}

/*******************************************************************\

Function: local_SSAt::output

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void local_SSAt::nodet::output(
  std::ostream &out,
  const namespacet &ns) const
{
  for(equalitiest::const_iterator
      e_it=equalities.begin();
      e_it!=equalities.end();
      e_it++)
    out << "(E) " << from_expr(ns, "", *e_it) << "\n";

  for(constraintst::const_iterator
      e_it=constraints.begin();
      e_it!=constraints.end();
      e_it++)
    out << "(C) " << from_expr(ns, "", *e_it) << "\n";

  if(assertion.is_not_nil())
    out << "(A) " << from_expr(ns, "", assertion) << "\n";
}

/*******************************************************************\

Function: local_SSAt::has_static_lifetime

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool local_SSAt::has_static_lifetime(const ssa_objectt &object) const
{
  return has_static_lifetime(object.get_expr());
}

/*******************************************************************\

Function: local_SSAt::has_static_lifetime

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool local_SSAt::has_static_lifetime(const exprt &src) const
{
  if(src.id()==ID_dereference)
    return true;
  else if(src.id()==ID_index)
    return has_static_lifetime(to_index_expr(src).array());
  else if(src.id()==ID_member)
    return has_static_lifetime(to_member_expr(src).struct_op());
  else if(src.id()==ID_symbol)
  {
    const symbolt &s=ns.lookup(to_symbol_expr(src));
    return s.is_static_lifetime;
  }
  else
    return false;
}

/*******************************************************************\

Function: local_SSAt::operator <<

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::list<exprt> & operator << (
  std::list<exprt> &dest,
  const local_SSAt &src)
{
  forall_goto_program_instructions(i_it, src.goto_function.body)
  {
    const local_SSAt::nodest::const_iterator n_it=
      src.nodes.find(i_it);
    if(n_it==src.nodes.end()) continue;

    for(local_SSAt::nodet::equalitiest::const_iterator
        e_it=n_it->second.equalities.begin();
        e_it!=n_it->second.equalities.end();
        e_it++)
    {
      dest.push_back(*e_it);
    }

    for(local_SSAt::nodet::constraintst::const_iterator
        c_it=n_it->second.constraints.begin();
        c_it!=n_it->second.constraints.end();
        c_it++)
    {
      dest.push_back(*c_it);
    }
  }
  
  return dest;
}

/*******************************************************************\

Function: local_SSAt::operator <<

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

decision_proceduret & operator << (
  decision_proceduret &dest,
  const local_SSAt &src)
{
  forall_goto_program_instructions(i_it, src.goto_function.body)
  {
    const local_SSAt::nodest::const_iterator n_it=
      src.nodes.find(i_it);
    if(n_it==src.nodes.end()) continue;

    for(local_SSAt::nodet::equalitiest::const_iterator
        e_it=n_it->second.equalities.begin();
        e_it!=n_it->second.equalities.end();
        e_it++)
    {
      dest << *e_it;
    }

    for(local_SSAt::nodet::constraintst::const_iterator
        c_it=n_it->second.constraints.begin();
        c_it!=n_it->second.constraints.end();
        c_it++)
    {
      dest << *c_it;
    }
  }
  
  return dest;
}

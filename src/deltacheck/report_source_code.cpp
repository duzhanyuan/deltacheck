/*******************************************************************\

Module: Source Code Reporting

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

//#define DEBUG

#include <util/i2string.h>

#include "report_source_code.h"
#include "html_escape.h"
#include "get_source.h"
#include "source_diff.h"
#include "syntax_highlighting.h"

/*******************************************************************\

Function: get_errors

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string get_errors(
  const propertiest &properties,
  const linet &line)
{
  std::string errors;

  for(propertiest::const_iterator
      p_it=properties.begin(); p_it!=properties.end(); p_it++)
  {
    if(line.file==p_it->loc->location.get_file() &&
       i2string(line.line_no)==as_string(p_it->loc->location.get_line()))
    {
      if(p_it->status.is_false())
      {
        if(!errors.empty()) errors+="<br>";

        irep_idt property=p_it->loc->location.get_property();
        irep_idt comment=p_it->loc->location.get_comment();

        if(comment=="")
          errors+=as_string(property);
        else
          errors+=as_string(comment);
      }
    }
  }
  
  return errors;
}

/*******************************************************************\

Function: report_source_code

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void report_source_code(
  const std::string &path_prefix,
  const locationt &location,
  const goto_programt &goto_program,
  const propertiest &properties,
  std::ostream &out,
  message_handlert &message_handler)
{
  std::list<linet> lines;
  get_source(path_prefix, location, goto_program, lines, message_handler);
  
  out << "<p>\n";
  out << "<table class=\"source\"><tr>\n";
  
  // error marking  

  out << "<td class=\"line_numbers\"><pre>\n";
  
  for(std::list<linet>::const_iterator
      l_it=lines.begin(); l_it!=lines.end(); l_it++)
  {
    std::string errors=get_errors(properties, *l_it);
    if(!errors.empty())
    {
      out << "<span style=\"color:#CC0000\" onmouseover=\"tooltip.show('"
          << html_escape(errors) << "');\""
                 " onmouseout=\"tooltip.hide();\">"
          << "&#x2717;"
          << "</span>";
    }
    out << "\n";
  }
    
  out << "</pre></td>\n";

  // line numbers go into a column
  
  out << "<td class=\"line_numbers\"><pre>\n";
  
  for(std::list<linet>::const_iterator
      l_it=lines.begin(); l_it!=lines.end(); l_it++)
    out << l_it->line_no << "\n";
    
  out << "</pre></td>\n";
  
  // now do source text in another column
  
  out << "<td class=\"code\"><pre>\n";
  
  syntax_highlightingt syntax_highlighting(out);
  
  for(std::list<linet>::const_iterator
      l_it=lines.begin(); l_it!=lines.end(); l_it++)
  {
    syntax_highlighting.line_no=l_it->line_no;
    syntax_highlighting(l_it->line);
  }
  
  out << "</pre></td></tr>\n";
  
  out << "</table>\n";
  out << "</p>\n";
}

/*******************************************************************\

Function: report_source_code

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void report_source_code(
  const std::string &path_prefix_old,
  const locationt &location_old,
  const goto_programt &goto_program_old,
  const std::string &path_prefix,
  const locationt &location,
  const goto_programt &goto_program,
  const propertiest &properties,
  std::ostream &out,
  message_handlert &message_handler)
{
  // get sources
  std::list<linet> lines, lines_old;

  get_source(path_prefix_old, location, goto_program, lines, message_handler);
  get_source(path_prefix, location_old, goto_program_old, lines_old, message_handler);

  // run 'diff'
  
  diff_it(lines_old, lines);
  
  out << "<p>\n";
  out << "<table class=\"source\">\n";  
  out << "<tr><th colspan=2>old version</th>"
         "<th colspan=3>new version</th></tr>\n";
  
  out << "<tr>\n";
  
  // old version

 std::list<linet>::const_iterator l_old_it, l_it;
 
  out << "<td class=\"line_numbers\"><pre>\n";
  
  for(std::list<linet>::const_iterator
      l_old_it=lines_old.begin(); l_old_it!=lines_old.end(); l_old_it++)
  {
    if(l_old_it->line_no!=0) out << l_old_it->line_no;
    out << "\n";
  }
    
  out << "</pre></td>\n";
  
  out << "<td class=\"code\"><pre>\n";

  {  
    syntax_highlightingt syntax_highlighting(out);
    
    for(l_old_it=lines_old.begin(), l_it=lines.begin();
        l_old_it!=lines_old.end() && l_it!=lines.end();
        l_old_it++, l_it++)
    {
      syntax_highlighting.different=(l_old_it->line!=l_it->line);
      syntax_highlighting.line_no=l_it->line_no;
      syntax_highlighting.id_suffix="_old";
      syntax_highlighting(l_old_it->line);
    }
  }
  
  out << "</pre></td>\n";
  
  // new version
  
  // error marking  
  out << "<td class=\"line_numbers\"><pre>\n";
  
  for(std::list<linet>::const_iterator
      l_it=lines.begin(); l_it!=lines.end(); l_it++)
  {
    std::string errors=get_errors(properties, *l_it);
    if(!errors.empty())
    {
      out << "<span style=\"color:#CC0000\" onmouseover=\"tooltip.show('"
          << html_escape(errors) << "');\""
                 " onmouseout=\"tooltip.hide();\">"
          << "&#x2717;"
          << "</span>";
    }
    out << "\n";
  }
    
  out << "</pre></td>\n";

  out << "<td class=\"line_numbers\"><pre>\n";
  
  for(std::list<linet>::const_iterator
      l_it=lines.begin(); l_it!=lines.end(); l_it++)
  {
    if(l_it->line_no!=0) out << l_it->line_no;
    out << "\n";
  }
    
  out << "</pre></td>\n";

  
  out << "<td class=\"code\"><pre>\n";
  
  {
    syntax_highlightingt syntax_highlighting(out);
  
    for(l_old_it=lines_old.begin(), l_it=lines.begin();
        l_old_it!=lines_old.end() && l_it!=lines.end();
        l_old_it++, l_it++)
    {
      syntax_highlighting.different=(l_old_it->line!=l_it->line);
      syntax_highlighting.line_no=l_it->line_no;
      syntax_highlighting(l_it->line);
    }
  }
  
  out << "</pre></td></tr>\n";
  
  out << "</table>\n";
  out << "</p>\n";  
}


How to configure
================

To use liblognorm, you need 3 things.

1. An installed and working copy of liblognorm. The installation process 
   has been discussed in the chapter :doc:`installation`.
2. Log files.
3. A rulebase, which is heart of liblognorm configuration.

Log files
---------

A log file is a text file, which typically holds many lines. Each line is 
a log message. These are usually a bit strange to read, thus to analyze. 
This mostly happens, if you have a lot of different devices, that are all 
creating log messages in a different format. 

Rulebase
--------

The rulebase holds all the schemes for your logs. It basically consists of 
many lines that reflect the structure of your log messages. When the 
normalization process is started, a parse-tree will be generated from
the rulebase and put into the memory. This will then be used to parse the 
log messages.

Each line in rulebase file is evaluated separately.

Commentaries
------------

To keep your rulebase tidy, you can use commentaries. Start a commentary 
with "#" like in many other configurations. It should look like this::

    # The following prefix and rules are for firewall logs
    
Note that the comment character MUST be in the first column of the line.

Empty lines are just skipped, they can be inserted for readability.

Rules
-----

If the line starts with 'rule=', then it contains a rule. This line has
following format::

    rule=[<tag1>[,<tag2>...]]:<match description>

Everything before a colon is treated as comma-separated list of tags, which
will be attached to a match. After the colon, match description should be
given. It consists of string literals and field selectors. String literals
should match exactly, whereas field selectors may match variable parts
of a message.

A rule could look like this::

    rule=:%date:date-rfc3164% %host:word% %tag:char-to:\x3a%: no longer listening on %ip:ipv4%#%port:number%'

This excerpt is a common rule. A rule always contains several different 
"parts"/properties and reflects the structure of the message you want to 
normalize (e.g. Host, IP, Source, Syslogtag...).

Literals
--------

Literal is just a sequence of characters, which must match exactly. 
Percent sign characters must be escaped to prevent them from starting a 
field accidentally. Replace each "%" with "\\x25" or "%%", when it occurs
in a string literal.

Fields
------

The structure of a field selector is as follows::

    %<field name>:<field type>[:<additional information>]%

field name -> that name can be selected freely. It should be a description 
of what kind of information the field is holding, e.g. SRC is the field 
contains the source IP address of the message. These names should also be 
chosen carefully, since the field name can be used in every rule and 
therefore should fit for the same kind of information in different rules.

If field name is "-", then this field is matched but not saved.

field type -> selects the accordant parser, which are described below.

Special characters that need to be escaped when used inside a field 
description are "%" and ":". For example, this will match anything up to
(but not including) a colon::

    %variable:char-to:\x3a%

Whitespace, including LF, is permitted inside a field definition after
the opening precent sign and before the closing one. This can be used to
make complex rules more readable. So the example rule from the overview
section above could be rewritten as::

    rule=:%
          date:date-rfc3164
          % %
	  host:word
	  % %
	  tag:char-to:\x3a
	  %: no longer listening on %
	  ip:ipv4
	  %#%
	  port:number
	  %'

When doing this, note well that whitespace IS important inside the
literal text. So e.g. in the second example line above "% %" we require
a single SP as literal text. Note that any combination of your liking is
valid, so it could also be written as::

    rule=:%date:date-rfc3164% %host:word% % tag:char-to:\x3a
          %: no longer listening on %  ip:ipv4  %#%  port:number  %'

To prevent a typical user error, continuation lines are **not** permitted
to start with ``rule=``. There are some obscure cases where this could
be a valid rule, and it can be re-formatted in that case. Moreoften, this
is the result of a missing percent sign, as in this sample::

     rule=:test%field:word ... missing percent sign ...
     rule=:%f:word%

If we would permit ``rule=`` at start of continuation line, these kinds
of problems would be very hard to detect.

Additional information is dependent on the field type; only some field 
types need additional information.
    
Field types
-----------

number
######

One or more decimal digits.

::

    %port:number%

float
#####

A floating-pt number represented in non-scientific form.

::

    %pause_time:float%

hexnumber
#########

A hexadecimal number as seen by this parser begins with the string
"0x", is followed by 1 or more hex digits and is terminated by white
space. Any interleaving non-hex digits will cause non-detection. The
rules are strict to avoid false positives.

::

    %session:hexnumber%

kernel-timestamp
################

Parses a linux kernel timestamp, which has the format

::

    [ddddd.dddddd]

where "d" is a decimal digit. The part before the period has to
have at least 5 digits as per kernel code. There is no upper
limit per se inside the kernel, but liblognorm does not accept
more than 12 digits, which seems more than sufficient (we may reduce
the max count if misdetections occur). The part after the period
has to have exactly 6 digits.

::

    %session:hexnumber%

whitespace
##########

This parses all whitespace until the first non-whitespace character
is found. This is primarily a tool to skip to the next "word" if
the exact number of whitspace characters (and type of whitespace)
is not known. The current parsing position MUST be on a whitspace,
else the parser does not match.

Remeber that to just parse but not preserve the field contents, the
dash ("-") is used as field name. This is almost always expected
with the *whitespace* syntax.

::

    %-:whitespace%

word
####    

One or more characters, up to the next space (\\x20), or
up to end of line.

::

    %host:word%

escaped-string
######

Parses a bash-style string, with quoting and escaping.
Characters can be escaped by preceding them with a backslash (\\).
Additionally, inside double quotes, two consecutive double quote
characters are converted to one double quote in the resulting string.

::

    %field-name:escaped-string%

string-to
######### 

One or more characters, up to the next string given in
extra data.

::

    %field_name:string-to:Auth%
    %field_name:string-to:Auth\x25%

alpha
#####   

One or more alphabetic characters, up to the next whitspace, punctuation,
decimal digit or control character.

::

    %host:alpha%

char-to
####### 

One or more characters, up to the next character given in
extra data. Additional data must contain exactly one character, which
can be escaped.

::

    %field_name:char-to:,%
    %field_name:char-to:\x25%

char-sep
########

Zero or more characters, up to the next character given in extra data, or 
up to end of line. Additional data must contain exactly one character, 
which can be escaped.               

::

    %field_name:char-sep:,%
    %field_name:char-sep:\x25%

rest
####

Zero or more characters till end of line. Must always be at end of the 
rule, even though this condition is currently **not** checked. In any case,
any definitions after *rest* are ignored.

Note that the *rest* syntax should be avoided because it generates
a very broad match. To mitigate this effect, the rest parser is always
only invoked if no other parser or string literal matches.

::

    %field_name:rest%

See also `Rainer's blog posting on the "rest" parser <http://blog.gerhards.net/2015/04/liblognorms-rest-parser-now-more-useful.html>`_. 

quoted-string
#############   

Zero or more characters, surrounded by double quote marks.
Quote marks are stripped from the match.

::

    %field_name:quoted-string%

op-quoted-string
################   


Zero or more characters, possibly surrounded by double quote marks.
If the first character is a quote mark, operates like quoted-string. Otherwise, operates like "word"
Quote marks are stripped from the match.

::

    %field_name:quoted-string%

date-iso
########    

Date in ISO format ('YYYY-MM-DD').

::

    %field-name:date-iso%

time-24hr
#########   

Time of format 'HH:MM:SS', where HH is 00..23.

::

    %time:time-24hr%

time-12hr
#########   

Time of format 'HH:MM:SS', where HH is 00..12.

::

    %time:time-12hr%

duration
########   

A duration is similar to a timestamp, except that
it tells about time elapsed. As such, hours can be larger than 23
and hours may also be specified by a single digit (this, for example,
is commonly done in Cisco software).

Examples for durations are "12:05:01", "0:00:01" and "37:59:59" but not
"00:60:00" (HH and MM must still be within the usual range for
minutes and seconds).

::

    %session_lasted:duration%

date-rfc3164
############

Valid date/time in RFC3164 format, i.e.: 'Oct 29 09:47:08'.
This parser implements several quirks to match malformed
timestamps from some devices.

::

    %date:date-rfc3164%

date-rfc5424
############

Valid date/time in RFC5424 format, i.e.:
'1985-04-12T19:20:50.52-04:00'.
Slightly different formats are allowed.

::

    %date:date-rfc5424%

ipv4
####

IPv4 address, in dot-decimal notation (AAA.BBB.CCC.DDD).

::

    %ip-src:ipv4%

ipv6
####

IPv6 address, in textual notation as specified in RFC4291.
All formats specified in section 2.2 are supported, including
embedded IPv4 address (e.g. "::13.1.68.3"). Note that a 
**pure** IPv4 address ("13.1.68.3") is **not** valid and as
such not recognized.

To avoid false positives, there must be either a whitespace
character after the IPv6 address or the end of string must be
reached.

::

    %ip-src:ipv6%

mac48
#####

The standard (IEEE 802) format for printing MAC-48 addresses in
human-friendly form is six groups of two hexadecimal digits,
separated by hyphens (-) or colons (:), in transmission order
(e.g. 01-23-45-67-89-ab or 01:23:45:67:89:ab ).
This form is also commonly used for EUI-64.
from: http://en.wikipedia.org/wiki/MAC_address

::

    %mac:mac48%

cef
###

This parses ArcSight Comment Event Format (CEF) as described in 
the "Implementing ArcSight CEF" manual revision 20 (2013-06-15).

It matches a format that closely follows the spec. The header fields
are extracted into the field name container, all extension are
extracted into a container called "Extensions" beneath it.

Example
.......

Rule::

    rule=:%f:cef'

Data::

    CEF:0|Vendor|Product|Version|Signature ID|some name|Severity| aa=field1 bb=this is a value cc=field 3

Result::

    {
      "f": {
        "DeviceVendor": "Vendor",
        "DeviceProduct": "Product",
        "DeviceVersion": "Version",
        "SignatureID": "Signature ID",
        "Name": "some name",
        "Severity": "Severity",
        "Extensions": {
          "aa": "field1",
          "bb": "this is a value",
          "cc": "field 3"
        }
      }
    }

checkpoint-lea
##############

This supports the LEA on-disk format. Unfortunately, the format
is underdocumented, the Checkpoint docs we could get hold of just
describe the API and provide a field dictionary. In a nutshell, what
we do is extract field names up to the colon and values up to the
semicolon. No escaping rules are known to us, so we assume none
exists (and as such no semicolon can be part of a value).

If someone has a definitive reference or a sample set to contribute
to the project, please let us know and we will check if we need to
add additional transformations.

::

    %fields:checkpoint-lea%

cisco-interface-spec
####################

A Cisco interface specifier, as for example seen in PIX or ASA.
The format contains a number of optional parts and is described
as follows (in ABNF-like manner where square brackets indicate
optional parts):

::

  [interface:]ip/port [SP (ip2/port2)] [[SP](username)]

Samples for such a spec are:

 * outside:192.168.52.102/50349
 * inside:192.168.1.15/56543 (192.168.1.112/54543)
 * outside:192.168.1.13/50179 (192.168.1.13/50179)(LOCAL\some.user)
 * outside:192.168.1.25/41850(LOCAL\RG-867G8-DEL88D879BBFFC8) 
 * inside:192.168.1.25/53 (192.168.1.25/53) (some.user)
 * 192.168.1.15/0(LOCAL\RG-867G8-DEL88D879BBFFC8)

Note that the current verision of liblognorm does not permit sole
IP addresses to be detected as a Cisco interface spec. However, we
are reviewing more Cisco message and need to decide if this is
to be supported. The problem here is that this would create a much
broader parser which would potentially match many things that are
**not** Cisco interface specs.

As this object extracts multiple subelements, it create a JSON
structure. 

Let's for example look at this definiton::

    %ifaddr:cisco-interface-spec%

and assume the following message is to be parsed::

 outside:192.168.1.13/50179 (192.168.1.13/50179) (LOCAL\some.user)

Then the resulting JSON will be as follows::

{ "ifaddr": { "interface": "outside", "ip": "192.168.1.13", "port": "50179", "ip2": "192.168.1.13", "port2": "50179", "user": "LOCAL\\some.user" } }

Subcomponents that are not given in the to-be-normalized string are
also not present in the resulting JSON.

tokenized
#########

Values of any field-type separated by some sort of token. 
It returns json array of tokens when matched.
Additional arguments are tokenizing subsequence, followed by 
expected type of single token.

Here is an expression that'd match IPv4 addresses separated 
by ', ' (comma + space). Given string "192.168.1.2, 192.168.1.3, 192.168.1.4"
it would produce: { my_ips: [ "192.168.1.2", "192.168.1.3", "192.168.1.4" ] }

::

    %my_ips:tokenized:, :ipv4%

However, it can be made multi-level deep by chaining. 
The expression below for instance, would match numbers 
sepated by '#' which occur in runs separated by ' : ' 
which occur in runs separated by ', '. 
So given "10, 20 : 30#40#50 : 60#70#80, 90 : 100"
it would produce: { some_nos: [ [ [ "10" ] ], [ [ "20" ], [ "30", "40", "50" ], 
[ "60", "70", "80" ] ], [ [ "90" ], [ "100" ] ] ] }

::
   
   %some_nos:tokenized:, :tokenized: \x3a :tokenized:#:number%

Note how colon (:) is used unescaped when using as field-pattern, but is escaped when 
used as tokenizer subsequence. The same would appply to use of % character.

recursive
#########

Value that matches some other rule defined in the same rulebase. Its called
recursive because it invokes the entire parser-tree again.

The invocation below will call the entire ruleset again and put the parsed
content under the key 'foo'.

::

    %foo:recursive%

However, matching initial fragment of text requires the remaining 
(suffix-fragment) portion of it to be matched and given back to 
original field so it can be matched by remaining portion of rule
which follows the matched fragmet(remember, it is being called to 
match only a portion of text from another rule). 

Additional argument can be passed to pick field-name to be used for 
returning unmatched text. It is optional, and defaults to 'tail'. The
example below uses 'remains' as the field name insteed of 'tail'.

::

    %foo:recursive:remains%

Recursive fields are often useful in combination with tokenized field.
This ruleset for instance, will match multiple IPv4 addresses or 
Subnets in expected message.

::

    rule=:%subnet_addr:ipv4%/%subnet_mask:number%%tail:rest%
    rule=:%ip_addr:ipv4%%tail:rest%
    rule=:blocked inbound via: %via_ip:ipv4% from: %addresses:tokenized:, :recursive% to %server_ip:ipv4%

Given "blocked inbound via: 192.168.1.1 from: 1.2.3.4, 16.17.18.0/8, 12.13.14.15, 19.20.21.24/3 to 192.168.1.5"
would produce: 

.. code-block:: json

  {
  "addresses": [
    {"ip_addr": "1.2.3.4"}, 
    {"subnet_addr": "16.17.18.0", "subnet_mask": "8"}, 
    {"ip_addr": "12.13.14.15"}, 
    {"subnet_addr": "19.20.21.24", "subnet_mask": "3"}], 
  "server_ip": "192.168.1.5",
  "via_ip": "192.168.1.1"}

Notice how 'tail' field is used in first two rules to capture unmatched 
text, which is then matched against the remaining portion of rule.
This example can be rewritten to use arbitrary field-name to capture 
unmatched portion of text. The example below is rewritten to use field 
'remains' to capture it insteed of 'tail'.

::

    rule=:%subnet_addr:ipv4%/%subnet_mask:number%%remains:rest%
    rule=:%ip_addr:ipv4%%remains:rest%
    rule=:blocked inbound via: %via_ip:ipv4% from: %addresses:tokenized:, :recursive:remains% to %server_ip:ipv4%

descent
#######

Value that matches some other rule defined in a different rulebase. Its called
descent because it descends down to a child rulebase and invokes the entire 
parser-tree again. Its like recursive, except it calls a different rulebase for
recursive parsing(as opposed to recursive which calls itself). It takes two 
arguments, first is the file name and second is optional argument explained 
below.

The invocation below will call the ruleset in /foo/bar.rulebase and put the 
parsed content under the key 'foo'.

::

    %foo:descent:/foo/bar.rulebase%

Like recursive, matching initial fragment of text requires the remaining 
(suffix-fragment) portion of it to be matched and given back to 
original field(this is explained in detail in documentation for recursive 
field).

Additional argument can be passed to pick field-name to be used for 
returning unmatched text. It is optional, and defaults to 'tail'. The
example below uses 'remains' as the field name insteed of 'tail'.

::

    %foo:descent:/foo/bar.rulebase:remains%

Like recursive, descent field is often useful in combination with tokenized 
field. The usage example for this would look very similar to that of recursive 
(with field declaration changing to include rulebase path).

This brings with it the overhead of having to maintain multiple rulebase files, 
but also helps alleviate complexity when a single ruleset becomes too complex.

regex
#####

Field matched by a given regex.

This internally uses PCRE (http://www.pcre.org/).

Note that regex based field is slower and computationally heavier
compared to other statically supported field-types. Because of potential
performance penalty, support for regex is disabled by default. It can be enabled
by providing appropriate options to tooling/library/scripting layer that interfaces with
liblognorm (for instance, by using '-oallowRegex' as a commandline arg with lognormalizer
or using 'allowRegex="on"' in rsyslog module load statement). In many cases use of regex
can be avoided by use of 'recursive' field.

Additional arguments are regular-expression (mandatory), followed by 2 optional arguments,
namely consume-group and return-group. Consume-group identifies the matched-subsequence
which will be treated as part of string consumed by the field, and the return-group is the 
part of string which the field returns (that is, the picked value for the field). Both 
consume-group and return-group default to 0(which is the portion matched by entire expression). 
If consume-group number is provided, return-group number defaults to consume-group as well.

Special characters occuring in regular-expression must be escaped.

Here is an example of regex based field declaration (with default consume and return group), 
which is equivallent to native field-type 'word'.

::

    %a_word:regex:[^ ]+%

Here is an expression that'd extract a numeric-sequence surrounded by some relevant text,
some of which we want to consume as a part of matching this field, and parts which we 
want to leave for next field to consume. With input "sales 200k with margin 6%"
this should produce: { margin_pct: "6", sale_worth: "200" }

::

    %sale_worth:regex:(sales (\d+)k with) margin:1:2% %margin_pct:regex:margin (\d+)\x25:0:1%

It can sometimes be useful in places where eger matching by native field-type-definitions
become a problem, such as trying to extract hostnames from this string "hostnames are foo.bar,
bar.baz, baz.quux". Using %hostnames:tokenized:, :word% doesn't work, becuase word ends up 
consuming the comma as well. So the using regex here can be helpful.

::

   hostnames are %hostnames:tokenized:, :regex:[^, ]+%

Note that consume-group must match content starting at the begining of string, else it wouldn't
be considererd matching anything at all.

interpret
#########

Meta field-type to re-interpret matched content as any supported type.

This field doesn't match text on its own, it just re-interprets the matched content and
passes it out as desired type. The matcher field-type is passed as one of the arguments to 
it.

It needs 2 additional options, the first is desired type that matched content should 
be re-interpreted to, and second is actual field-declaration which is used to match the content.

Special characters such as percent(%) and colon(:) occuring as a part of arguments to 
field-declaration must be escaped similar to first-class usage of the field.

Here is an example that shows how reinterpret field can be used to extract an integer from 
matched content.

::

    %count:interpret:int:word%

Here is a more elaborate example which extracts multiple integer and double values. 
(Note how latency_percentile field uses escaping, its no different from directly calling char-to).

::

    record count for shard [%shard:interpret:base16int:char-to:]%] is %record_count:interpret:base10int:number% and %latency_percentile:interpret:float:char-to:\x25%\x25ile latency is %latency:interpret:float:word% %latency_unit:word%

Given text "record count for shard [3F] is 50000 and 99.99%ile latency is 2.1 seconds" the 
above rule would produce the following:

.. code-block:: json

  {"shard": 63, 
   "record_count": 50000, 
   "latency_percentile": 99.99, 
   "latency": 2.1, 
   "latency_unit" : "seconds"}

To contrast this with a interpret-free version, the rule(without interpret) would look like:

::

    record count for shard [%shard:char-to:]%] is %record_count:number% and %latency_percentile:char-to:\x25%\x25ile latency is %latency:word% %latency_unit:word%

And would produce:

.. code-block:: json

  {"shard": "3F", 
   "record_count": "50000", 
   "latency_percentile": "99.99", 
   "latency": "2.1", 
   "latency_unit" : "seconds"}

Interpret fields is generally useful when generated json needs to be consumed by an indexing-system
of some kind (eg. database), because ordering and indexing mechanism of a string is very different from
that of a number or a boolean, and keeping it in its native type allows for powerful aggregation and 
querying.

Here is a table of supported interpretation:

+-----------+----------------------+---------------+----------------+
| type      | description          | matched value | returned value |
+-----------+----------------------+---------------+----------------+
| int       | integer value        | "100"         | 100            |
+-----------+----------------------+---------------+----------------+
| base10int | integer value        | "100"         | 100            |
+-----------+----------------------+---------------+----------------+
| base16int | integer value        | "3F"          | 163            |
+-----------+----------------------+---------------+----------------+
| float     | floating point value | "19.35"       | 19.35          |
+-----------+----------------------+---------------+----------------+
| bool      | boolean value        | "true"        | true           |
+-----------+----------------------+---------------+----------------+
|           |                      | "false"       | false          |
+-----------+----------------------+---------------+----------------+
|           |                      | "yes"         | true           |
+-----------+----------------------+---------------+----------------+
|           |                      | "no"          | false          |
+-----------+----------------------+---------------+----------------+
|           |                      | "TRUE"        | true           |
+-----------+----------------------+---------------+----------------+
|           |                      | "FALSE"       | false          |
+-----------+----------------------+---------------+----------------+

suffixed
########

Value that can be matched by any available field-type but also has one
of many suffixes which must be captured alongwith, for the captured data
to be used sensibly.

The invocation below will capture units alongwith quantity.

::

    %free_space:suffixed:,:b,kb,mb,gb:number%

It takes 3 arguments. First is delimiter for possible-suffixes enumeration,
second is the enumeration itself (separated by declared delimiter) and third
captures type to be used to parse the value itself.

It returns an object with key "value" which holds the parsed value and
a key "suffix" which captures which one of the provided suffixes was found
after it.

Here is an example that parses suffixed values:

::

    rule=:reclaimed %eden_reclaimed:suffixed:,:b,kb,mb,gb:number% from eden

Given text "reclaimed 115mb from eden" the 
above rule would produce:

.. code-block:: json

  {
    "eden_reclaimed":
      {
        "value": "115", 
        "suffix": "mb"
      }
  }

It can be used with interpret to actually get numeric values, and field-type
named_suffix field can be used if the default keys used are not sensible.

named_suffixed
##############

Works exactly like suffixed, but allows user to specify key-litterals for "value"
and "suffix" fields.

The invocation below will capture units alongwith quantity.

::

    %free_space:named_suffixed:mem:unit:,:b,kb,mb,gb:number%

It takes 5 arguments. First is the litteral to be used as key for parsed-value,
second is key-litteral for suffix, and list three which exactly match field-type
suffixed. Third is delimiter for possible-suffixes enumeration,
fourth is the enumeration itself (separated by declared delimiter)
and fifth captures type to be used to parse the value itself.

Here is an example that parses suffixed values:

::

    rule=:reclaimed %eden_reclaimed:named_suffixed:mem:unit:,:b,kb,mb,gb:number% from eden

Given text "reclaimed 115mb from eden" the 
above rule would produce:

.. code-block:: json

  {
    "eden_reclaimed":
      {
        "mem": "115", 
        "unit": "mb"
      }
  }


iptables
########    

Name=value pairs, separated by spaces, as in Netfilter log messages.
Name of the selector is not used; names from the line are 
used instead. This selector always matches everything till 
end of the line. Cannot match zero characters.

::

    %-:iptables%

cisco-interface-spec
####################

This is an experimental parser. It is used to detect Cisco Interface
Specifications. A sample of them is:

::

   outside:176.97.252.102/50349

Note that this parser does not yet extract the individual parts
due to the restrictions in current liblognorm. This is planned for
after a general algorithm overhaul.

In order to match, this syntax must start on a non-whitespace char
other than colon.

json
####
This parses native JSON from the message. All data up to the first non-JSON
is parsed into the field. There may be any other field after the JSON,
including another JSON section.

Note that any white space after the actual JSON
is considered **to be part of the JSON**. So you cannot filter on whitespace
after the JSON.

::

    %data:json%

Example
.......

Rule::

    rule=:%field1:json%interim text %field2:json%'

Data::

   {"f1": "1"} interim text {"f2": 2}

Result::

   { "field2": { "f2": 2 }, "field1": { "f1": "1" } }

Note also that the space before "interim" must **not** be given in the
rule, as it is consumed by the JSON parser. However, the space after
"text" is required.

cee-syslog
##########
This parses cee syslog from the message. This format has been defined
by Mitre CEE as well as Project Lumberjack.

This format essentially is JSON with additional restrictions:

 * The message must start with "@cee:"
 * an JSON **object** must immediately follow (whitespace before it permitted,
   but a JSON array is **not** permitted)
 * after the JSON, there must be no other non-whitespace characters.

In other words: the message must consist of a single JSON object only, 
prefixed by the "@cee:" cookie.

Note that the cee cookie is case sensitive, so "@CEE:" is **NOT** valid.

::

    %data:cee-syslog%

Prefixes
--------

Several rules can have a common prefix. You can set it once with this 
syntax::

    prefix=<prefix match description>
    
Prefix match description syntax is the same as rule match description. 
Every following rule will be treated as an addition to this prefix.

Prefix can be reset to default (empty value) by the line::

    prefix=

You can define a prefix for devices that produce the same header in each 
message. We assume, that you have your rules sorted by device. In such a 
case you can take the header of the rules and use it with the prefix 
variable. Here is a example of a rule for IPTables::

    prefix=%date:date-rfc3164% %host:word% %tag:char-to:-\x3a%:
    rule=:INBOUND%INBOUND:char-to:-\x3a%: IN=%IN:word% PHYSIN=%PHYSIN:word% OUT=%OUT:word% PHYSOUT=%PHYSOUT:word% SRC=%source:ipv4% DST=%destination:ipv4% LEN=%LEN:number% TOS=%TOS:char-to: % PREC=%PREC:word% TTL=%TTL:number% ID=%ID:number% DF PROTO=%PROTO:word% SPT=%SPT:number% DPT=%DPT:number% WINDOW=%WINDOW:number% RES=0x00 ACK SYN URGP=%URGP:number%

Usually, every rule would hold what is defined in the prefix at its 
beginning. But since we can define the prefix, we can save that work in 
every line and just make the rules for the log lines. This saves us a lot 
of work and even saves space.

In a rulebase you can use multiple prefixes obviously. The prefix will be 
used for the following rules. If then another prefix is set, the first one 
will be erased, and new one will be used for the following rules.

Rule tags
---------

Rule tagging capability permits very easy classification of syslog 
messages and log records in general. So you can not only extract data from 
your various log source, you can also classify events, for example, as 
being a "login", a "logout" or a firewall "denied access". This makes it 
very easy to look at specific subsets of messages and process them in ways 
specific to the information being conveyed. 

To see how it works, let’s first define what a tag is:

A tag is a simple alphanumeric string that identifies a specific type of 
object, action, status, etc. For example, we can have object tags for 
firewalls and servers. For simplicity, let’s call them "firewall" and 
"server". Then, we can have action tags like "login", "logout" and 
"connectionOpen". Status tags could include "success" or "fail", among 
others. Tags form a flat space, there is no inherent relationship between 
them (but this may be added later on top of the current implementation). 
Think of tags like the tag cloud in a blogging system. Tags can be defined 
for any reason and need. A single event can be associated with as many 
tags as required. 

Assigning tags to messages is simple. A rule contains both the sample of 
the message (including the extracted fields) as well as the tags. 
Have a look at this sample::

    rule=:sshd[%pid:number%]: Invalid user %user:word% from %src-ip:ipv4%

Here, we have a rule that shows an invalid ssh login request. The various 
fields are used to extract information into a well-defined structure. Have 
you ever wondered why every rule starts with a colon? Now, here is the 
answer: the colon separates the tag part from the actual sample part. 
Now, you can create a rule like this::

    rule=ssh,user,login,fail:sshd[%pid:number%]: Invalid user %user:word% from %src-ip:ipv4%

Note the "ssh,user,login,fail" part in front of the colon. These are the 
four tags the user has decided to assign to this event. What now happens 
is that the normalizer does not only extract the information from the 
message if it finds a match, but it also adds the tags as metadata. Once 
normalization is done, one can not only query the individual fields, but 
also query if a specific tag is associated with this event. For example, 
to find all ssh-related events (provided the rules are built that way), 
you can normalize a large log and select only that subset of the 
normalized log that contains the tag "ssh".

Log annotations
---------------

In short, annotations allow to add arbitrary attributes to a parsed
message, depending on rule tags. Values of these attributes are fixed,
they cannot be derived from variable fields. Syntax is as following::

    annotate=<tag>:+<field name>="<field value>"

Field value should always be enclosed in double quote marks.

There can be multiple annotations for the same tag.

Examples
--------

Look at :doc:`sample rulebase <sample_rulebase>` for configuration 
examples and matching log lines. 

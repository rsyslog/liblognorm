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

Additional information is dependent on the field type; only some field 
types need additional information.
    
Field types
-----------

number
######

One or more decimal digits.

::

    %port:number%

word
####    

One or more characters, up to the next space (\\x20), or
up to end of line.

::

    %host:word%

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

Zero or more characters till end of line. Should be always at end of the 
rule.

::

    %field_name:rest%

quoted-string
#############   

Zero or more characters, surrounded by double quote marks.
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

regex
#####

Field matched by a given regex.

This internally uses PCRE (http://www.pcre.org/).

Note that regex based field is slower and computationally heavier
compared to other statically supported field-types. Because of potential
performance penalty, support for regex is disabled by default. It can be enabled
by providing appropriate options to tooling/library/scripting layer that interfaces with
liblognorm (for instance, by using '-oallowRegex' as a commandline arg with lognormalizer
or using 'allowRegex="on"' in rsyslog module load statement).

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


iptables
########    

Name=value pairs, separated by spaces, as in Netfilter log messages.
Name of the selector is not used; names from the line are 
used instead. This selector always matches everything till 
end of the line. Cannot match zero characters.

::

    %-:iptables%

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

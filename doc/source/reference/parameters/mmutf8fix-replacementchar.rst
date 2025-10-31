.. _param-mmutf8fix-replacementchar:
.. _mmutf8fix.parameter.input.replacementchar:

replacementChar
===============

.. index::
   single: mmutf8fix; replacementChar
   single: replacementChar

.. summary-start

Defines the printable character used to substitute invalid sequences.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/mmutf8fix`.

:Name: replacementChar
:Scope: input
:Type: string
:Default: " "
:Required?: no
:Introduced: 7.5.4

Description
-----------
This is the character that invalid sequences are replaced by. It is
strongly recommended to use a printable US-ASCII character. Note that
only the first byte of the provided string is used without validation.

Input usage
-----------
.. _mmutf8fix.parameter.input.replacementchar-usage:

.. code-block:: rsyslog

   module(load="mmutf8fix")

   action(type="mmutf8fix" replacementChar="#")

See also
--------
See also :doc:`../../configuration/modules/mmutf8fix`.

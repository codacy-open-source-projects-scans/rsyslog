.. _param-mmutf8fix-mode:
.. _mmutf8fix.parameter.input.mode:

mode
====

.. index::
   single: mmutf8fix; mode
   single: mode

.. summary-start

Selects how invalid byte sequences are detected and replaced.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/mmutf8fix`.

:Name: mode
:Scope: input
:Type: string
:Default: "utf-8"
:Required?: no
:Introduced: 7.5.4

Description
-----------
Sets the basic detection mode for invalid byte sequences.

``"utf-8"`` (default)
    Checks for proper UTF-8 encoding. Bytes that form invalid UTF-8
    sequences are replaced with the character defined by
    :ref:`param-mmutf8fix-replacementchar`. This applies to various
    invalid conditions, including:

    * Invalid start bytes or stray continuation bytes.
    * A multi-byte sequence that is incomplete.
    * A complete sequence that is invalid (e.g., overlong encoding, a
      disallowed codepoint like a UTF-16 surrogate, or a value > U+10FFFF).

    Control characters are not replaced because they are valid UTF-8.
    This mode is most useful with non-US-ASCII character sets, which
    validly include multibyte sequences.

``"controlcharacters"``
    Replaces all bytes that do not represent a printable US-ASCII
    character (codes 32 to 126) with the character defined by
    :ref:`param-mmutf8fix-replacementchar`. This invalidates valid
    UTF-8 multi-byte sequences and should be used only when characters
    outside the US-ASCII range are not expected.

Input usage
-----------
.. _mmutf8fix.parameter.input.mode-usage:

.. code-block:: rsyslog

   module(load="mmutf8fix")

   action(type="mmutf8fix" mode="controlcharacters")

See also
--------
See also :doc:`../../configuration/modules/mmutf8fix`.

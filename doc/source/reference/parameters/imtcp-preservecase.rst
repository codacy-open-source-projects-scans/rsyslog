.. _param-imtcp-preservecase:
.. _imtcp.parameter.module.preservecase:
.. _imtcp.parameter.input.preservecase:

PreserveCase
============

.. index::
   single: imtcp; PreserveCase
   single: PreserveCase

.. summary-start

Controls whether the case of fromhost is preserved.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/imtcp`.

:Name: PreserveCase
:Scope: module, input
:Type: boolean
:Default: module=on, input=module parameter
:Required?: no
:Introduced: 8.37.0

Description
-----------
This parameter is for controlling the case in fromhost.  If preservecase is set to "off", the case in fromhost is not preserved.  E.g., 'host1.example.org' the message was received from 'Host1.Example.Org'.  Default to "on" for the backward compatibility.

The same-named input parameter can override this module setting.


Module usage
------------
.. _param-imtcp-module-preservecase:
.. _imtcp.parameter.module.preservecase-usage:

.. code-block:: rsyslog

   module(load="imtcp" preserveCase="off")

Input usage
-----------
.. _param-imtcp-input-preservecase:
.. _imtcp.parameter.input.preservecase-usage:

.. code-block:: rsyslog

   input(type="imtcp" port="514" preserveCase="off")

See also
--------
See also :doc:`../../configuration/modules/imtcp`.


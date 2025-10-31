.. _param-imklog-internalmsgfacility:
.. _imklog.parameter.module.internalmsgfacility:

InternalMsgFacility
===================

.. index::
   single: imklog; InternalMsgFacility
   single: InternalMsgFacility

.. summary-start

Sets the facility used for messages that imklog generates internally.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/imklog`.

:Name: InternalMsgFacility
:Scope: module
:Type: facility
:Default: Linux: "kern"; other platforms: "syslogd"
:Required?: no
:Introduced: at least 5.x, possibly earlier

Description
-----------
This parameter sets the facility for messages that are generated internally by imklog.
imklog generates some messages of itself (e.g. on problems, startup and
shutdown) and these do not stem from the kernel. Historically, under
Linux, these too have "kern" facility. Thus, on Linux platforms the
default is "kern" while on others it is "syslogd". It is recommended to
not change this setting unless you have a specific reason to do so.

Module usage
------------
.. _param-imklog-module-internalmsgfacility:
.. _imklog.parameter.module.internalmsgfacility-usage:

.. code-block:: rsyslog

   module(load="imklog" internalMsgFacility="kern")

Legacy names (for reference)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Historic names/directives for compatibility. Do not use in new configs.

.. _imklog.parameter.legacy.kloginternalmsgfacility:

- $KLogInternalMsgFacility — maps to InternalMsgFacility (status: legacy)

.. index::
   single: imklog; $KLogInternalMsgFacility
   single: $KLogInternalMsgFacility

See also
--------
:doc:`../../configuration/modules/imklog`

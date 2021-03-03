
=====================================
Installing NovelVM 
=====================================

This page explains how to install NovelVM on a computer. For all other platforms, see the relevant :ref:`platform guide <platformspecific>`.

.. tabbed:: Windows

    There are two ways to install NovelVM on Windows: use the installer, or install manually. 

    .. dropdown:: Installing NovelVM using the installer
        :open:

        Download the Windows installer for your operating system from the `NovelVM downloads page <https://www.novelvm.org/downloads/>`_, and double click the downloaded file. The installer guides you through the install process, and adds a shortcut to the Start Menu. There is also an option to add a shortcut to the desktop. 

        To run NovelVM, either navigate to desktop and double click the NovelVM shortcut, or go to **Start > All Apps > NovelVM**. For Windows XP, go to **Start > All Apps > NovelVM**.

    .. dropdown:: Installing NovelVM manually
          
        Download the Windows zip file for your operating system (32bit or 64bit). To extract the files, right-click the folder and select **Extract All**. 

        To run NovelVM from the extracted folder, find the ``novelvm.exe`` file and double click it. 

.. tabbed:: macOS

    .. dropdown:: Installing NovelVM using the disk image
        :open:

        Download the recommended disk image file from the `NovelVM downloads page <https://www.novelvm.org/downloads/>`_. After the download has completed, double click the file to mount the disk image file. A window containing the NovelVM icon opens. Drag this icon into your Applications folder to install NovelVM.

        To run NovelVM, click on the icon in the Applications folder.

        .. note::

            macOS includes technology called Gatekeeper, which checks to ensure only trusted software is run on your Mac. NovelVM is not available from the App Store, so follow the steps on this `Apple support page <https://support.apple.com/en-us/HT202491>`_ to allow NovelVM to run. 
    
.. tabbed:: Linux


   There are multiple ways to install NovelVM on Linux: use the Snap Store, Flathub or the software repository, or manually install the release binary. 

    .. dropdown::  Installing NovelVM using the Snap Store
        :open:

        A Snap is an app that is bundled with its dependencies, which makes the install on any Linux operating system very easy. Snap comes pre-installed on Debian and Ubuntu-based distributions, but can be installed on any Linux distribution by following the instructions on the `Snapcraft website <https://snapcraft.io/>`_.

        The NovelVM Snap comes with a selection of freeware games and demos pre-loaded. 

        Enter the following on the command line to install the NovelVM Snap:

        .. code:: bash

            sudo snap install novelvm

        To run NovelVM, enter ``novelvm`` on the command line, or launch NovelVM through the desktop interface by clicking **Menu > Games > NovelVM**.

    .. dropdown:: Installing NovelVM using Flathub

        Flathub is another way to install apps for Linux, by using Flatpak. Flatpak comes standard with Fedora-based distributions, but can be installed on any Linux operating system.  The `Flathub website <https://flatpak.org/setup/>`_ has excellent install instructions.

        When Flatpak is installed, enter the following on the command line to install NovelVM:

        .. code:: bash

            flatpak install flathub org.novelvm.NovelVM

        Some distributions have the option to install Flatpaks through the graphical desktop interface. Navigate to the `NovelVM Flatpak page <https://flathub.org/apps/details/org.novelvm.NovelVM>`_ , click the **INSTALL** button and then follow the install process. 

        To run NovelVM, enter the following on the command line:

        .. code:: bash

            flatpak run org.novelvm.NovelVM

        To pass :doc:`Command line arguments <../advanced_topics/command_line>`, add them after the Flatpak ``run`` command.

        .. note:: 

            The Flatpak version of NovelVM is sandboxed, meaning that any games need to be copied into the Documents folder to be accessible by NovelVM. 

      
    .. dropdown:: Installing NovelVM using the software repository

        NovelVM is found in the software repositories of many Linux distributions. 

        .. caution::

            The repositories might not contain the most up-to-date version of NovelVM. 

        To run NovelVM, enter ``novelvm`` on the command line, or launch NovelVM through the desktop interface by clicking **Menu > Games > NovelVM**.


    .. dropdown:: Installing NovelVM using the release binaries
        
        Binary packages are only released for Debian and Ubuntu. On the `NovelVM downloads page <https://www.novelvm.org/downloads/>`_, find and download the NovelVM package that corresponds to your operating system and architecture. To install a DEB package, either double click on the downloaded DEB file to use the graphical installer, or, if that's not available, use the command line.

        .. code:: bash

            sudo apt install /path/to/downloaded/file.deb

        Replace ``/path/to/downloaded/file.deb`` with the actual path to the downloaded DEB package. The APT software manager handles the installation. 

        To run NovelVM, enter ``novelvm`` on the command line, or launch NovelVM through the desktop interface by clicking **Menu > Games > NovelVM**.


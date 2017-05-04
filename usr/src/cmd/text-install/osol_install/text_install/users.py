#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
#

'''
Set root password and primary user data
'''

import logging
import os
import os.path

from osol_install.profile.user_info import UserInfo
from osol_install.text_install import _
from osol_install.text_install.base_screen import BaseScreen, UIMessage
from osol_install.text_install.edit_field import EditField
from osol_install.text_install.error_window import ErrorWindow
from osol_install.text_install.list_item import ListItem
from osol_install.text_install.window_area import WindowArea
from osol_install.text_install.i18n import textwidth


class UserScreen(BaseScreen):
    '''Allow user to set:
    - root password
    - user real name
    - user login
    - user password
    
    '''
    SONICLE_USER = os.path.isdir("/sonicle")

    HEADER_TEXT = _("Users")
    INTRO = _("Define a root password for the system.")
    ROOT_TEXT = _("System Root Password")
    ROOT_LABEL = _("Root password:")
    CONFIRM_LABEL = _("Confirm password:")
    USER_TEXT = _("Create a user account")
    NAME_LABEL = _("Your real name:")
    USERNAME_LABEL = _("Username:")
    USER_PASS_LABEL = _("User password:")
    
    NO_ROOT_HEADER = _("No Root Password")
    NO_ROOT_TEXT = _("A root password has not been defined. The system is "
                     "completely unsecured.\n\nChoose Cancel to set a "
                     "root password.")
    NO_USER_HEADER = _("No User Password")
    NO_USER_TEXT = _("A user password has not been defined. The user account "
                     "has administrative privileges so the system is "
                     "unsecured.\n\nChoose Cancel to set a user password.")
    CONTINUE_BTN = _("Continue")
    CANCEL_BTN = _("Cancel")
    
    PASS_SCREEN_LEN = 16
    ITEM_OFFSET = 2
    
    def __init__(self, main_win):
        super(UserScreen, self).__init__(main_win)

        self.max_text_len = (self.win_size_x - UserScreen.PASS_SCREEN_LEN -
                             UserScreen.ITEM_OFFSET) / 2
        max_field = max(textwidth(UserScreen.ROOT_LABEL),
                        textwidth(UserScreen.CONFIRM_LABEL),
                        textwidth(UserScreen.NAME_LABEL),
                        textwidth(UserScreen.USERNAME_LABEL),
                        textwidth(UserScreen.USER_PASS_LABEL))
        self.text_len = min(max_field + 1, self.max_text_len)
        self.list_area = WindowArea(1, self.text_len, 0,
                                    UserScreen.ITEM_OFFSET)
        scrollable_columns = UserInfo.MAX_PASS_LEN + 1
        self.edit_area = WindowArea(1, UserScreen.PASS_SCREEN_LEN + 1,
                                    0, self.text_len,
                                    scrollable_columns=scrollable_columns)
        self.username_edit_area = WindowArea(1, 
                                    UserInfo.MAX_USERNAME_LEN + 1,
                                    0, self.text_len)
        err_x_loc = 2 * self.max_text_len - self.text_len
        err_width = (self.text_len + UserScreen.PASS_SCREEN_LEN)
        self.error_area = WindowArea(1, err_width, 0, err_x_loc)
        self.root = None
        self.user = None
        self.root_pass_list = None
        self.root_pass_edit = None
        self.root_pass_err = None
        self.root_confirm_err = None
        self.root_confirm_list = None
        self.root_confirm_edit = None
        self.real_name_err = None
        self.real_name_list = None
        self.real_name_edit = None
        self.username_err = None
        self.username_list = None
        self.username_edit = None
        self.user_pass_err = None
        self.user_pass_list = None
        self.user_pass_edit = None
        self.user_confirm_err = None
        self.user_confirm_list = None
        self.user_confirm_edit = None
    
    def _show(self):
        '''Display the user name, real name, and password fields'''
        if len(self.install_profile.users) != 2:
            root = UserInfo()
            root.login_name = "root"
            root.is_role = True
            user = UserInfo()
            self.install_profile.users = [root, user]
        self.root = self.install_profile.users[0]
        self.user = self.install_profile.users[1]
        
        logging.debug("Root: %s", self.root)
        logging.debug("User: %s", self.user)
        
        y_loc = 1
        
        self.center_win.add_paragraph(UserScreen.INTRO, y_loc, 1, y_loc + 2,
                                      self.win_size_x - 1)
        
        y_loc += 3
        self.center_win.add_text(UserScreen.ROOT_TEXT, y_loc, 1,
                                 self.win_size_x - 1)
        
        y_loc += 2
        self.error_area.y_loc = y_loc
        self.list_area.y_loc = y_loc
        self.root_pass_err = ErrorWindow(self.error_area,
                                         window=self.center_win)
        self.root_pass_list = ListItem(self.list_area, window=self.center_win,
                                       text=UserScreen.ROOT_LABEL)
        self.root_pass_edit = EditField(self.edit_area,
                                        window=self.root_pass_list,
                                        error_win=self.root_pass_err,
                                        masked=True,
                                        text=("*" * self.root.passlen))
        self.root_pass_edit.clear_on_enter = True
        
        y_loc += 1
        self.error_area.y_loc = y_loc
        self.list_area.y_loc = y_loc
        self.root_confirm_err = ErrorWindow(self.error_area,
                                            window=self.center_win)
        self.root_confirm_list = ListItem(self.list_area,
                                          window=self.center_win,
                                          text=UserScreen.CONFIRM_LABEL)
        self.root_confirm_edit = EditField(self.edit_area,
                                           window=self.root_confirm_list,
                                           masked=True,
                                           text=("*" * self.root.passlen),
                                           on_exit=pass_match,
                                           error_win=self.root_confirm_err)
        self.root_confirm_edit.clear_on_enter = True
        rc_edit_kwargs = {"linked_win" : self.root_pass_edit}
        self.root_confirm_edit.on_exit_kwargs = rc_edit_kwargs
        
	if not UserScreen.SONICLE_USER:
            y_loc += 2
            self.center_win.add_text(UserScreen.USER_TEXT, y_loc, 1,
                                 self.win_size_x - 1)
        
            y_loc += 2
            self.list_area.y_loc = y_loc
            self.error_area.y_loc = y_loc
            self.real_name_err = ErrorWindow(self.error_area,
                                         window=self.center_win)
            self.real_name_list = ListItem(self.list_area, window=self.center_win,
                                       text=UserScreen.NAME_LABEL)
            self.real_name_edit = EditField(self.edit_area,
                                        window=self.real_name_list,
                                        error_win=self.real_name_err,
                                        text=self.user.real_name)
        
            y_loc += 1
            self.list_area.y_loc = y_loc
            self.error_area.y_loc = y_loc
            self.username_err = ErrorWindow(self.error_area,
                                        window=self.center_win)
            self.username_list = ListItem(self.list_area,
                                      window=self.center_win,
                                      text=UserScreen.USERNAME_LABEL)
            self.username_edit = EditField(self.username_edit_area,
                                       window=self.username_list,
                                       validate=username_valid_alphanum,
                                       error_win=self.username_err,
                                       on_exit=username_valid,
                                       text=self.user.login_name)
        
            y_loc += 1
            self.list_area.y_loc = y_loc
            self.error_area.y_loc = y_loc
            self.user_pass_err = ErrorWindow(self.error_area,
                                         window=self.center_win)
            self.user_pass_list = ListItem(self.list_area, window=self.center_win,
                                       text=UserScreen.USER_PASS_LABEL)
            self.user_pass_edit = EditField(self.edit_area,
                                        window=self.user_pass_list,
                                        error_win=self.user_pass_err,
                                        masked=True,
                                        text=("*" * self.user.passlen))
            self.user_pass_edit.clear_on_enter = True
        
            y_loc += 1
            self.list_area.y_loc = y_loc
            self.error_area.y_loc = y_loc
            self.user_confirm_err = ErrorWindow(self.error_area,
                                            window=self.center_win)
            self.user_confirm_list = ListItem(self.list_area,
                                          window=self.center_win,
                                          text=UserScreen.CONFIRM_LABEL)
            self.user_confirm_edit = EditField(self.edit_area,
                                           window=self.user_confirm_list,
                                           masked=True, on_exit=pass_match,
                                           error_win=self.user_confirm_err,
                                           text=("*" * self.user.passlen))
            self.user_confirm_edit.clear_on_enter = True
            uc_edit_kwargs = {"linked_win" : self.user_pass_edit}
            self.user_confirm_edit.on_exit_kwargs = uc_edit_kwargs
        
        self.main_win.do_update()
        self.center_win.activate_object(self.root_pass_list)
    
    def on_change_screen(self):
        '''Save real name and login name always'''
	if not UserScreen.SONICLE_USER:
            self.user.real_name = self.real_name_edit.get_text()
            self.user.login_name = self.username_edit.get_text()

        self.root.is_role = bool(self.user.login_name)

        if self.root_pass_edit.get_text() != self.root_confirm_edit.get_text():
            self.root.password = ""
        else:
            self.root.password = self.root_pass_edit.get_text()
        self.root_pass_edit.clear_text()
        self.root_confirm_edit.clear_text()
        
	if not UserScreen.SONICLE_USER:
            if self.user_pass_edit.get_text() != self.user_confirm_edit.get_text():
                self.user.password = ""
            else:
                self.user.password = self.user_pass_edit.get_text()
            self.user_pass_edit.clear_text()
            self.user_confirm_edit.clear_text()
    
    def validate(self):
        '''Check for mismatched passwords, bad login names, etc.'''
        if self.root_pass_edit.get_text() != self.root_confirm_edit.get_text():
            raise UIMessage, _("Root passwords don't match")

	if not UserScreen.SONICLE_USER:
            user_pass = self.user_pass_edit.get_text()
            if user_pass != self.user_confirm_edit.get_text():
                raise UIMessage, _("User passwords don't match")
            login_name = self.username_edit.get_text()
            logging.debug("login_name=%s", login_name)
            username_valid(self.username_edit)
            real_name = self.real_name_edit.get_text()
            logging.debug("real_name=%s", real_name)
            if not login_name:
                if real_name or user_pass:
                    raise UIMessage, _("Enter username or clear all user "
                                    "account fields")
        color = self.main_win.theme.header
        if not self.root_pass_edit.get_text():
            continue_anyway = self.main_win.pop_up(UserScreen.NO_ROOT_HEADER,
                                                   UserScreen.NO_ROOT_TEXT,
                                                   BaseScreen.CANCEL_BUTTON,
                                                   UserScreen.CONTINUE_BTN,
                                                   color=color)
            if not continue_anyway:
                raise UIMessage()

	if not UserScreen.SONICLE_USER:
            if login_name and not user_pass:
                continue_anyway = self.main_win.pop_up(UserScreen.NO_USER_HEADER,
                                                       UserScreen.NO_USER_TEXT,
                                                       BaseScreen.CANCEL_BUTTON,
                                                       UserScreen.CONTINUE_BTN,
                                                       color=color)
                if not continue_anyway:
                    raise UIMessage()


def username_valid_alphanum(edit_field):
    '''Ensure username is alphanumeric, and does not start with a digit'''
    user_str = edit_field.get_text()
    if not user_str:
        return True
    if user_str[0].isalpha():
        if user_str.isalnum():
            return True
        else:
            raise UIMessage, _("Username must be alphanumeric")
    else:
        raise UIMessage, _("First character must be a-zA-Z")


def username_valid(edit_field):
    '''Ensure the username is not "root" or "jack" or "sonicle"'''
    user_str = edit_field.get_text()
    if user_str == "root":
        raise UIMessage, _("'root' cannot be used")
    elif user_str == "jack": 
        raise UIMessage, _("'jack' cannot be used")
    elif user_str == "sonicle":
        raise UIMessage, _("'sonicle' cannot be used")
    return True


def pass_match(edit_field, linked_win=None):
    '''Make sure passwords match'''
    confirm_pass = edit_field.get_text()
    if linked_win is None or linked_win.get_text() == confirm_pass:
        return True
    else:
        edit_field.clear_text()
        linked_win.clear_text()
        raise UIMessage, _("Passwords don't match")

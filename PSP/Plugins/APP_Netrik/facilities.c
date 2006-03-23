/*
   netrik -- The ANTRIK Internet Viewer
   Copyright (C) Olaf D. Buddenhagen AKA antrik, et al (see AUTHORS)
   Published under the GNU GPL; see LICENSE for details.
*/
/*
 * facilities.c -- contains all information about element types, attribute
 * types and entity references.
 *
 * (C) 2001, 2002 antrik
 *     2001, 2002 Patrice Neff
 *
 * necessary for syntax & element parsing, and to make something useful from
 * the syntax tree.
 *
 * In the future there will be more data-driven parameters, to make adding
 * elements or altering their behaviour easier.
 *
 * There are only very few elements and attributes implemented for now.
 */
#include "syntax.h"

/* miscallenous information about all element types
 * {name, breaks, force_box, group} */
/* update syntax.h also! */
const struct Element_data	element_table[]={
   {"html", 0, 0, GROUP_HTML, 1}, {"head", 0, 0, GROUP_HEAD, 0}, {"body", 0, 0, GROUP_HEAD, 1},
   {"title", 0, 0, GROUP_OBLIGATE, 0}, {"meta", 0, 0, GROUP_SINGLE, 0}, {"style", 0, 0, GROUP_OBLIGATE, 0}, {"script", 0, 0, GROUP_OBLIGATE, 0},
   {"h1", 2, 0, GROUP_OBLIGATE, 1}, {"h2", 2, 0, GROUP_OBLIGATE, 1}, {"h3", 2, 0, GROUP_OBLIGATE, 1}, {"h4", 2, 0, GROUP_OBLIGATE, 1}, {"h5", 2, 0, GROUP_OBLIGATE, 1}, {"h6", 2, 0, GROUP_OBLIGATE, 1},
   {"p", 2, 0, GROUP_PARAGRAPH, 1},
   {"em", 0, 0, GROUP_OBLIGATE, 1}, {"i", 0, 0, GROUP_OBLIGATE, 1},
   {"strong", 0, 0, GROUP_OBLIGATE, 1}, {"b", 0, 0, GROUP_OBLIGATE, 1},
   {"center", 1, 0, GROUP_OBLIGATE, 1},
   {"a", 0, 0, GROUP_OBLIGATE, 1},
   {"br", 0, 0, GROUP_SINGLE, 1},    /* no longer creates new text block (a '\n' is stored instead) */
   {"pre", 2, 0, GROUP_OBLIGATE, 1},
   {"table", 2, 1, GROUP_OBLIGATE, 1},
   {"tr", 1, 0, GROUP_TABLE_ROW, 1},
   {"td", 0, 0, GROUP_TABLE_CELL, 1}, {"th", 0, 0, GROUP_TABLE_CELL, 1},
   {"ul", 2, 0, GROUP_OBLIGATE, 1}, {"li", 1, 0, GROUP_LIST_ITEM, 1},
   {"ol", 2, 0, GROUP_OBLIGATE, 1},
   {"dl", 2, 0, GROUP_OBLIGATE, 1}, {"dt", 1, 0, GROUP_LIST_ITEM, 1}, {"dd", 1, 0, GROUP_LIST_ITEM, 1},
   {"hr", 1, 0, GROUP_SINGLE, 1},
   {"ins", 0, 0, GROUP_OBLIGATE, 1}, {"del", 0, 0, GROUP_OBLIGATE, 1},
   {"u", 0, 0, GROUP_OBLIGATE, 1}, {"s", 0, 0, GROUP_OBLIGATE, 1}, {"strike", 0, 0, GROUP_OBLIGATE, 1},
   {"form", 2, 1, GROUP_OBLIGATE, 1}, {"input", 0, 0, GROUP_SINGLE, 1}, {"select", 0, 0, GROUP_OBLIGATE, 1}, {"button", 0, 0, GROUP_OBLIGATE, 1}, {"option", 0, 0, GROUP_OPTION, 1}, {"textarea", 0, 0, GROUP_OBLIGATE, 0},
   {"img", 0, 0, GROUP_SINGLE, 1},
   {"div", 1, 0, GROUP_OBLIGATE, 1}, {"span", 0, 0, GROUP_OBLIGATE, 1},
   {"?", 0, 0, GROUP_SINGLE, 1},    /* making all unknown elements SINGLE we are on the safe side (otherwise, unknown single tags will "capture" following elements) */
   {"!", 1, 1, GROUP_OBLIGATE, 1}
};

/* update syntax.h also! */
/* if the same attribute exists for a specific element and for EL_NO, put the
   specific one first */
const struct Attr_data	attr_table[]={
   /* name, numeric, el, def_val */
   {"name", 0, EL_NO, ""},
   {"href", 0, EL_NO, ""},
   {"title", 0, EL_NO, ""},
   {"colspan", 0, EL_NO, ""},
   {"align", 0, EL_NO, ""},
   {"type", 0, EL_INPUT, "text"},
   {"size", 1, EL_INPUT, "10"},
   {"alt", 0, EL_NO, ""},
   {"value", 0, EL_NO, ""},
   {"cols", 1, EL_TEXTAREA, "50"},
   {"rows", 1, EL_TEXTAREA, "1"},
   {"id", 0, EL_NO, ""},
   {"action", 0, EL_FORM, ""},
   {"method", 0, EL_FORM, "GET"},
   {"enctype", 0, EL_NO, ""},
   {"checked", 0, EL_NO, ""},
   {"multiple", 0, EL_NO, ""},
   {"selected", 0, EL_NO, ""},
   {"?", 0, EL_NO, ""}
};

const struct Ref	ref_table[]={
   {"lt",'<'},
   {"gt",'>'},
   {"amp",'&'},
   {"quot",'"'},
   {"apos",'\''},

   /* all iso-8859-1 non-ascii chars */
   {"nbsp",160},
   {"iexcl",'\xa1'},
   {"cent",'\xa2'},
   {"pound",'\xa3'},
   {"curren",'\xa4'},
   {"yen",'\xa5'},
   {"brvbar",'\xa6'},
   {"sect",'\xa7'},
   {"uml",'\xa8'},
   {"copy",'\xa9'},
   {"ordf",'\xaa'},
   {"laquo",'\xab'},
   {"not",'\xac'},
   {"shy",'\xad'},
   {"reg",'\xae'},
   {"macr",'\xaf'},
   {"deg",'\xb0'},
   {"plusmn",'\xb1'},
   {"sup2",'\xb2'},
   {"sup3",'\xb3'},
   {"acute",'\xb4'},
   {"micro",'\xb5'},
   {"para",'\xb6'},
   {"middot",'\xb7'},
   {"cedil",'\xb8'},
   {"sup1",'\xb9'},
   {"ordm",'\xba'},
   {"raquo",'\xbb'},
   {"frac14",'\xbc'},
   {"frac12",'\xbd'},
   {"frac34",'\xbe'},
   {"iquest",'\xbf'},
   {"Agrave",'\xc0'},
   {"Aacute",'\xc1'},
   {"Acirc",'\xc2'},
   {"Atilde",'\xc3'},
   {"Auml",'\xc4'},
   {"Aring",'\xc5'},
   {"AElig",'\xc6'},
   {"Ccedil",'\xc7'},
   {"Egrave",'\xc8'},
   {"Eacute",'\xc9'},
   {"Ecirc",'\xca'},
   {"Euml",'\xcb'},
   {"Igrave",'\xcc'},
   {"Iacute",'\xcd'},
   {"Icirc",'\xce'},
   {"Iuml",'\xcf'},
   {"ETH",'\xd0'},
   {"Ntilde",'\xd1'},
   {"Ograve",'\xd2'},
   {"Oacute",'\xd3'},
   {"Ocirc",'\xd4'},
   {"Otilde",'\xd5'},
   {"Ouml",'\xd6'},
   {"times",'\xd7'},
   {"Oslash",'\xd8'},
   {"Ugrave",'\xd9'},
   {"Uacute",'\xda'},
   {"Ucirc",'\xdb'},
   {"Uuml",'\xdc'},
   {"Yacute",'\xdd'},
   {"THORN",'\xde'},
   {"szlig",'\xdf'},
   {"agrave",'\xe0'},
   {"aacute",'\xe1'},
   {"acirc",'\xe2'},
   {"atilde",'\xe3'},
   {"auml",'\xe4'},
   {"aring",'\xe5'},
   {"aelig",'\xe6'},
   {"ccedil",'\xe7'},
   {"egrave",'\xe8'},
   {"eacute",'\xe9'},
   {"ecirc",'\xea'},
   {"euml",'\xeb'},
   {"igrave",'\xec'},
   {"iacute",'\xed'},
   {"icirc",'\xee'},
   {"iuml",'\xef'},
   {"eth",'\xf0'},
   {"ntilde",'\xf1'},
   {"ograve",'\xf2'},
   {"oacute",'\xf3'},
   {"ocirc",'\xf4'},
   {"otilde",'\xf5'},
   {"ouml",'\xf6'},
   {"divide",'\xf7'},
   {"oslash",'\xf8'},
   {"ugrave",'\xf9'},
   {"uacute",'\xfa'},
   {"ucirc",'\xfb'},
   {"uuml",'\xfc'},
   {"yacute",'\xfd'},
   {"thorn",'\xfe'},
   {"yuml",'\xff'},
/* not implemented yet 
   {"Alpha",''},
   {"alpha",''},
   {"Beta",''},
   {"beta",''},
   {"Gamma",''},
   {"gamma",''},
   {"Delta",''},
   {"delta",''},
   {"Epsilon",''},
   {"epsilon",''},
   {"Zeta",''},
   {"zeta",''},
   {"Eta",''},
   {"eta",''},
   {"Theta",''},
   {"theta",''},
   {"Iota",''},
   {"iota",''},
   {"Kappa",''},
   {"kappa",''},
   {"Lambda",''},
   {"lambda",''},
   {"Mu",''},
   {"mu",''},
   {"Nu",''},
   {"nu",''},
   {"Xi",''},
   {"xi",''},
   {"Omicron",''},
   {"omicron",''},
   {"Pi",''},
   {"pi",''},
   {"Rho",''},
   {"rho",''},
   {"Sigma",''},
   {"sigmaf",''},
   {"sigma",''},
   {"Tau",''},
   {"tau",''},
   {"Upsilon",''},
   {"upsilon",''},
   {"Phi",''},
   {"phi",''},
   {"Chi",''},
   {"chi",''},
   {"Psi",''},
   {"psi",''},
   {"Omega",''},
   {"omega",''},
   {"thetasym",''},
   {"upsih",''},
   {"piv",''},
*/
   {"",0}    /* table end */
};

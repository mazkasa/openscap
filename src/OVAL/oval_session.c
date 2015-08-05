/*
 * Copyright 2015 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:
 * 		Michal Šrubař <msrubar@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "common/alloc.h"
#include "common/util.h"
#include "common/_error.h"
#include "common/oscapxml.h"
#include "source/xslt_priv.h"
#include "public/oval_agent_api.h"
#include "public/oval_session.h"
#include "../DS/public/ds_sds_session.h"
#include "oscap_source.h"

struct oval_session {
	/* Main source assigned with the main file (SDS or OVAL) */
	struct oscap_source *source;
	struct oval_definition_model *def_model;

	struct ds_sds_session *sds_session;

	struct {
		struct oscap_source *definitions;
		struct oscap_source *variables;
		struct oscap_source *directives;
	} oval;

	const char *datastream_id;
	/* particular OVAL component if there are two OVALs in one datastream */
	const char *component_id;

	struct {
		const char *results;
		const char *report;
	} export;

	struct {
		/* it's called when there is something invalid in input/output files */
		xml_reporter xml_fn;
	} reporter;

	bool validation;
	bool full_validation;
};

struct oval_session *oval_session_new(const char *filename)
{
	oscap_document_type_t scap_type;
	struct oval_session *session;

	session = (struct oval_session *) oscap_calloc(1, sizeof(struct oval_session));

	session->source = oscap_source_new_from_file(filename);
	if ((scap_type = oscap_source_get_scap_type(session->source)) == OSCAP_DOCUMENT_UNKNOWN) {
		oval_session_free(session);
		return NULL;
	}

	if (scap_type != OSCAP_DOCUMENT_OVAL_DEFINITIONS && scap_type != OSCAP_DOCUMENT_SDS) {
		oscap_seterr(OSCAP_EFAMILY_OSCAP, "Session input file was determined but it"
				" isn't an OVAL file nor a source datastream file.");
		oval_session_free(session);
		return NULL;
	}

	return session;
}

void oval_session_set_variables(struct oval_session *session, const char *filename)
{
	__attribute__nonnull__(session);

	oscap_source_free(session->oval.variables);

	if (filename != NULL)
		session->oval.variables = oscap_source_new_from_file(filename);
	else
		session->oval.variables = NULL;	/* reset */
}

void oval_session_set_directives(struct oval_session *session, const char *filename)
{
	__attribute__nonnull__(session);

	oscap_source_free(session->oval.directives);

	if (filename != NULL)
		session->oval.directives = oscap_source_new_from_file(filename);
	else
		session->oval.directives = NULL;
}

void oval_session_set_validation(struct oval_session *session, bool validate, bool full_validation)
{
	__attribute__nonnull__(session);

	session->validation = validate;
	session->full_validation = full_validation;
}

void oval_session_set_datastream_id(struct oval_session *session, const char *id)
{
	__attribute__nonnull__(session);

	oscap_free(session->datastream_id);
	session->datastream_id = oscap_strdup(id);
}

void oval_session_set_component_id(struct oval_session *session, const char *id)
{
	__attribute__nonnull__(session);

	oscap_free(session->component_id);
	session->component_id = oscap_strdup(id);
}

void oval_session_set_results_export(struct oval_session *session, const char *filename)
{
	__attribute__nonnull__(session);

	oscap_free(session->export.results);
	session->export.results = oscap_strdup(filename);
}

void oval_session_set_report_export(struct oval_session *session, const char *filename)
{
	__attribute__nonnull__(session);

	oscap_free(session->export.report);
	session->export.report = oscap_strdup(filename);
}

void oval_session_set_xml_reporter(struct oval_session *session, xml_reporter fn)
{
	__attribute__nonnull__(session);

	session->reporter.xml_fn = fn;
}

static bool oval_session_validate(struct oval_session *session, struct oscap_source *source, oscap_document_type_t type)
{
	if (oscap_source_get_scap_type(source) == type) {
		if (oscap_source_validate(source, session->reporter.xml_fn, NULL))
			return false;
	}
	else {
		oscap_seterr(OSCAP_EFAMILY_OVAL, "Type mismatch: %s. Expecting %s "
			"but found %s.", oscap_source_readable_origin(source),
			oscap_document_type_to_string(type),
			oscap_document_type_to_string(oscap_source_get_scap_type(source)));
		return false;
	}

	return true;
}

static int oval_session_load_definitions(struct oval_session *session)
{
	__attribute__nonnull__(session);
	__attribute__nonnull__(session->source);

	oscap_document_type_t type = oscap_source_get_scap_type(session->source);
	if (type != OSCAP_DOCUMENT_OVAL_DEFINITIONS && type != OSCAP_DOCUMENT_SDS) {
		oscap_seterr(OSCAP_EFAMILY_OVAL, "Type mismatch: %s. Expecting %s "
			"or %s but found %s.", oscap_source_readable_origin(session->source),
			oscap_document_type_to_string(OSCAP_DOCUMENT_OVAL_DEFINITIONS),
			oscap_document_type_to_string(OSCAP_DOCUMENT_SDS),
			oscap_document_type_to_string(type));
		return 1;
	}
	else {
		if (!oval_session_validate(session, session->source, type))
			return 1;
	}

	if (oscap_source_get_scap_type(session->source) == OSCAP_DOCUMENT_SDS) {
		if ((session->sds_session = ds_sds_session_new_from_source(session->source)) == NULL) {
			return 1;
		}

		ds_sds_session_set_datastream_id(session->sds_session, session->datastream_id);
		if (ds_sds_session_register_component_with_dependencies(session->sds_session,
					"checks", session->component_id, "oval.xml") != 0) {
			return 1;
		}

		session->oval.definitions = ds_sds_session_get_component_by_href(session->sds_session, "oval.xml");
		if (session->oval.definitions == NULL) {
			oscap_seterr(OSCAP_EFAMILY_OVAL, "Internal error: OVAL file was not found in "
					"Source DataStream session cache!");
			return 1;
		}
	}
	else {
		session->oval.definitions = session->source;
	}

	/* import OVAL Definitions */
	if (session->def_model) oval_definition_model_free(session->def_model);
	session->def_model = oval_definition_model_import_source(session->oval.definitions);
	if (session->def_model == NULL) {
		oscap_seterr(OSCAP_EFAMILY_OVAL, "Failed to import the OVAL Definitions from '%s'.",
				oscap_source_readable_origin(session->oval.definitions));
		return 1;
	}

	return 0;
}

void oval_session_free(struct oval_session *session)
{
	if (session == NULL)
		return;

	oscap_source_free(session->oval.directives);
	oscap_source_free(session->oval.variables);
	oscap_source_free(session->source);
	oscap_free(session->datastream_id);
	oscap_free(session->component_id);
	oscap_free(session->export.results);
	oscap_free(session->export.report);
	if (session->def_model)
		oval_definition_model_free(session->def_model);
	ds_sds_session_free(session->sds_session);
	oscap_free(session);
}

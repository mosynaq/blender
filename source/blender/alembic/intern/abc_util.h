/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Esteban Tovagliari, Cedric Paille, Kevin Dietrich
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __ABC_UTIL_H__
#define __ABC_UTIL_H__

#include <Alembic/Abc/All.h>
#include <Alembic/AbcGeom/All.h>

#ifdef _MSC_VER
#  define ABC_INLINE static __forceinline
#else
#  define ABC_INLINE static inline
#endif

struct CacheReader {
	int unused;
};

using Alembic::Abc::chrono_t;

class AbcObjectReader;
struct ImportSettings;

struct ID;
struct Object;

std::string get_id_name(ID *id);
std::string get_id_name(Object *ob);
std::string get_object_dag_path_name(Object *ob, Object *dupli_parent);

bool object_selected(Object *ob);
bool parent_selected(Object *ob);

Imath::M44d convert_matrix(float mat[4][4]);
void create_transform_matrix(float r_mat[4][4]);
void create_transform_matrix(Object *obj, float transform_mat[4][4]);

void split(const std::string &s, const char delim, std::vector<std::string> &tokens);

template<class TContainer>
bool begins_with(const TContainer &input, const TContainer &match)
{
	return input.size() >= match.size()
	        && std::equal(match.begin(), match.end(), input.begin());
}

void convert_matrix(const Imath::M44d &xform, Object *ob,
                    float r_mat[4][4], float scale, bool has_alembic_parent = false);

template <typename Schema>
void get_min_max_time_ex(const Schema &schema, chrono_t &min, chrono_t &max)
{
	const Alembic::Abc::TimeSamplingPtr &time_samp = schema.getTimeSampling();

	if (!schema.isConstant()) {
		const size_t num_samps = schema.getNumSamples();

		if (num_samps > 0) {
			const chrono_t min_time = time_samp->getSampleTime(0);
			min = std::min(min, min_time);

			const chrono_t max_time = time_samp->getSampleTime(num_samps - 1);
			max = std::max(max, max_time);
		}
	}
}

template <typename Schema>
void get_min_max_time(const Alembic::AbcGeom::IObject &object, const Schema &schema, chrono_t &min, chrono_t &max)
{
	get_min_max_time_ex(schema, min, max);

	const Alembic::AbcGeom::IObject &parent = object.getParent();
	if (parent.valid() && Alembic::AbcGeom::IXform::matches(parent.getMetaData())) {
		Alembic::AbcGeom::IXform xform(parent, Alembic::AbcGeom::kWrapExisting);
		get_min_max_time_ex(xform.getSchema(), min, max);
	}
}

bool has_property(const Alembic::Abc::ICompoundProperty &prop, const std::string &name);

float get_weight_and_index(float time,
                           const Alembic::AbcCoreAbstract::TimeSamplingPtr &time_sampling,
                           int samples_number,
                           Alembic::AbcGeom::index_t &i0,
                           Alembic::AbcGeom::index_t &i1);

AbcObjectReader *create_reader(const Alembic::AbcGeom::IObject &object, ImportSettings &settings);

/* ************************** */

/* TODO(kevin): for now keeping these transformations hardcoded to make sure
 * everything works properly, and also because Alembic is almost exclusively
 * used in Y-up software, but eventually they'll be set by the user in the UI
 * like other importers/exporters do, to support other axis. */

/* Copy from Y-up to Z-up. */

ABC_INLINE void copy_zup_from_yup(float zup[3], const float yup[3])
{
	zup[0] = yup[0];
	zup[1] = -yup[2];
	zup[2] = yup[1];
}

ABC_INLINE void copy_zup_from_yup(short zup[3], const short yup[3])
{
	zup[0] = yup[0];
	zup[1] = -yup[2];
	zup[2] = yup[1];
}

/* Copy from Z-up to Y-up. */

ABC_INLINE void copy_yup_from_zup(float yup[3], const float zup[3])
{
	yup[0] = zup[0];
	yup[1] = zup[2];
	yup[2] = -zup[1];
}

ABC_INLINE void copy_yup_from_zup(short yup[3], const short zup[3])
{
	yup[0] = zup[0];
	yup[1] = zup[2];
	yup[2] = -zup[1];
}

/* *************************** */

#undef ABC_DEBUG_TIME

class ScopeTimer {
	const char *m_message;
	double m_start;

public:
	ScopeTimer(const char *message);
	~ScopeTimer();
};

#ifdef ABC_DEBUG_TIME
#	define SCOPE_TIMER(message) ScopeTimer prof(message)
#else
#	define SCOPE_TIMER(message)
#endif

#endif  /* __ABC_UTIL_H__ */
